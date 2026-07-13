// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-input.h"

#include "zc/core/encoding.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/internal/binding-skeleton.h"
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
    auto retainedTokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(retainedTokens != zc::none);
    ZC_IF_SOME(value, retainedTokens) { tokens = zc::mv(value); }
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
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
};

parser::ParsedTokenSnapshot parseTokenSnapshot(source::SourceManager& sources,
                                               const source::BufferId& buffer) {
  diagnostics::DiagnosticEngine diagnostics(sources);
  basic::LangOptions options;
  basic::StringPool strings;
  parser::Parser parser(sources, diagnostics, options, strings, buffer);
  ZC_REQUIRE(parser.parse() != zc::none);
  auto tokens = parser.takeTokenSnapshot();
  ZC_REQUIRE(tokens != zc::none);
  ZC_IF_SOME(value, tokens) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("parser token snapshot fixture failed");
}

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

    ZC_REQUIRE(sourceFixture.tokens != zc::none);
    auto retainedTokens = zc::mv(ZC_ASSERT_NONNULL(sourceFixture.tokens));
    auto admission =
        ParsedModuleVerifier::admit(snapshot, *sourceFixture.sources, sourceFixture.buffer,
                                    zc::mv(retainedTokens), zc::mv(sourceFixture.tree));
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

identity::DefId requireDefinitionTarget(const BindingTarget& target) {
  ZC_REQUIRE(target.value().is<DefinitionBindingTarget>());
  return target.value().get<DefinitionBindingTarget>().definition;
}

identity::DefId requireScopeDefinitionInNamespace(zc::ArrayPtr<const ScopeRecord> scopes,
                                                  zc::StringPtr name, Namespace nameSpace,
                                                  size_t occurrence = 0) {
  size_t matching = 0;
  for (const auto& scope : scopes) {
    for (const auto& binding : scope.bindings) {
      if (binding.name.nameSpace() != nameSpace || binding.name.name().text() != name) { continue; }
      if (matching++ == occurrence) {
        return requireDefinitionTarget(binding.binding.bindingIdentity);
      }
    }
  }
  ZC_FAIL_REQUIRE("named scope definition is missing");
}

identity::DefId requireScopeDefinition(zc::ArrayPtr<const ScopeRecord> scopes, zc::StringPtr name,
                                       size_t occurrence = 0) {
  return requireScopeDefinitionInNamespace(scopes, name, Namespace::Value, occurrence);
}

zc::Vector<ast::NodeId> identifierExpressions(const ast::Tree& tree, zc::StringPtr name) {
  zc::Vector<ast::NodeId> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::IdentExpr) { return; }
    const ast::IdentId identifier(syntax.payload.words[ast::kIdentExprNameWord]);
    if (tree.ident(identifier) == name) { result.add(node); }
  });
  return result;
}

zc::Vector<ast::NodeId> nodesOfKind(const ast::Tree& tree, ast::SyntaxKind kind) {
  zc::Vector<ast::NodeId> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind == kind) { result.add(node); }
  });
  return result;
}

zc::Vector<ast::NodeId> identifierPaths(const ast::Tree& tree, ast::SyntaxKind kind,
                                        zc::StringPtr name) {
  ZC_REQUIRE(kind == ast::SyntaxKind::ModulePath || kind == ast::SyntaxKind::AttributePath);
  zc::Vector<ast::NodeId> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != kind) { return; }
    const ast::IdentList segments =
        kind == ast::SyntaxKind::ModulePath
            ? ast::IdentList{syntax.payload.words[ast::kModulePathSegmentsFirstWord],
                             syntax.payload.words[ast::kModulePathSegmentsSizeWord]}
            : ast::IdentList{syntax.payload.words[ast::kAttributePathSegmentsFirstWord],
                             syntax.payload.words[ast::kAttributePathSegmentsSizeWord]};
    if (!tree.contains(segments)) { return; }
    const auto identifiers = tree.identList(segments);
    if (identifiers.size() == 1 && tree.ident(identifiers[0]) == name) { result.add(node); }
  });
  return result;
}

zc::Vector<ast::NodeId> shorthandProperties(const ast::Tree& tree, zc::StringPtr name) {
  zc::Vector<ast::NodeId> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::ObjectProperty ||
        syntax.payload.words[ast::kObjectPropertyShortFormWord] == 0) {
      return;
    }
    const ast::IdentId identifier(syntax.payload.words[ast::kObjectPropertyNameWord]);
    if (tree.ident(identifier) == name) { result.add(node); }
  });
  return result;
}

const BindingResolution& requireResolution(zc::ArrayPtr<const BindingResolution> resolutions,
                                           ast::NodeId node) {
  for (const auto& resolution : resolutions) {
    if (resolution.node == node) { return resolution; }
  }
  ZC_FAIL_REQUIRE("identifier resolution is missing");
}

BindingResolution& requireResolution(zc::ArrayPtr<BindingResolution> resolutions,
                                     ast::NodeId node) {
  for (auto& resolution : resolutions) {
    if (resolution.node == node) { return resolution; }
  }
  ZC_FAIL_REQUIRE("identifier resolution is missing");
}

const ControlTransferFact& requireControlTransfer(zc::ArrayPtr<const ControlTransferFact> facts,
                                                  ast::NodeId node) {
  for (const auto& fact : facts) {
    if (fact.node == node) { return fact; }
  }
  ZC_FAIL_REQUIRE("control-transfer fact is missing");
}

ControlTransferFact& requireControlTransfer(zc::ArrayPtr<ControlTransferFact> facts,
                                            ast::NodeId node) {
  for (auto& fact : facts) {
    if (fact.node == node) { return fact; }
  }
  ZC_FAIL_REQUIRE("mutable control-transfer fact is missing");
}

ControlTarget cloneControlTarget(const ControlTarget& target) {
  if (target.is<LoopControlTarget>()) {
    return ControlTarget(LoopControlTarget{target.get<LoopControlTarget>().scope});
  }
  if (target.is<MatchControlTarget>()) {
    return ControlTarget(MatchControlTarget{target.get<MatchControlTarget>().scope});
  }
  ZC_FAIL_REQUIRE("explicit label targets are unavailable before label cutover");
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.byteStart() == right.byteStart() && left.byteEnd() == right.byteEnd();
}

const FrozenDefinitionEntry& requireFrozenDefinition(const VerifiedBindingInput& input,
                                                     identity::DefId identity) {
  for (const auto& entry : input.definitions().definitions()) {
    if (entry.definition == identity) { return entry; }
  }
  ZC_FAIL_REQUIRE("frozen definition is missing");
}

bool encodedBytesLess(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

bool sameEncodedBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
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

ZC_TEST("ParsedModule.RetainsExactEscapedKeywordTokenSpans") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { \\u0062reak; br\\u0065ak; cont\\u0069nue; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  ZC_REQUIRE(fixture.parsed != zc::none);
  ZC_IF_SOME(parsed, fixture.parsed) {
    const auto breaks = nodesOfKind(parsed.tree(), ast::SyntaxKind::BreakStmt);
    const auto continues = nodesOfKind(parsed.tree(), ast::SyntaxKind::ContinueStatement);
    ZC_REQUIRE(breaks.size() == 2);
    ZC_REQUIRE(continues.size() == 1);
    auto leadingBreakToken = parsed.retainedTokenSpan(breaks[0], 0, ast::SyntaxKind::BreakKeyword);
    auto middleBreakToken = parsed.retainedTokenSpan(breaks[1], 0, ast::SyntaxKind::BreakKeyword);
    auto continueToken =
        parsed.retainedTokenSpan(continues[0], 0, ast::SyntaxKind::ContinueKeyword);
    ZC_REQUIRE(leadingBreakToken != zc::none);
    ZC_REQUIRE(middleBreakToken != zc::none);
    ZC_REQUIRE(continueToken != zc::none);
    ZC_IF_SOME(span, leadingBreakToken) { ZC_EXPECT(span.byteEnd() - span.byteStart() == 10); }
    ZC_IF_SOME(span, middleBreakToken) { ZC_EXPECT(span.byteEnd() - span.byteStart() == 10); }
    ZC_IF_SOME(span, continueToken) { ZC_EXPECT(span.byteEnd() - span.byteStart() == 13); }
    ZC_EXPECT(parsed.retainedTokenSpan(breaks[0], 0, ast::SyntaxKind::ContinueKeyword) == zc::none);
  }
}

ZC_TEST("ParsedModule.RetainsLabelTokenOrdinalsAndExactSourceLocations") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { out\\u0065r: while (true) { br\\u0065ak out\\u0065r; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  ZC_REQUIRE(fixture.parsed != zc::none);
  ZC_IF_SOME(parsed, fixture.parsed) {
    const auto labels = nodesOfKind(parsed.tree(), ast::SyntaxKind::LabeledStatement);
    const auto breaks = nodesOfKind(parsed.tree(), ast::SyntaxKind::BreakStmt);
    ZC_REQUIRE(labels.size() == 1);
    ZC_REQUIRE(breaks.size() == 1);
    auto declaration = parsed.retainedTokenSpan(labels[0], 0, ast::SyntaxKind::Identifier);
    auto keyword = parsed.retainedTokenSpan(breaks[0], 0, ast::SyntaxKind::BreakKeyword);
    auto reference = parsed.retainedTokenSpan(breaks[0], 1, ast::SyntaxKind::Identifier);
    ZC_REQUIRE(declaration != zc::none);
    ZC_REQUIRE(keyword != zc::none);
    ZC_REQUIRE(reference != zc::none);
    ZC_IF_SOME(span, declaration) {
      ZC_EXPECT(span.byteEnd() - span.byteStart() == 10);
      auto location = parsed.sourceLocFor(span);
      ZC_REQUIRE(location != zc::none);
      ZC_IF_SOME(value, location) {
        ZC_EXPECT(value == parsed.tree().node(labels[0]).range.getStart());
      }
    }
    ZC_IF_SOME(span, keyword) {
      ZC_EXPECT(span.byteEnd() - span.byteStart() == 10);
      auto location = parsed.sourceLocFor(span);
      ZC_REQUIRE(location != zc::none);
      ZC_IF_SOME(value, location) {
        ZC_EXPECT(value == parsed.tree().node(breaks[0]).range.getStart());
      }
    }
    ZC_IF_SOME(span, reference) {
      ZC_EXPECT(span.byteEnd() - span.byteStart() == 10);
      auto location = parsed.sourceLocFor(span);
      ZC_REQUIRE(location != zc::none);
      ZC_IF_SOME(value, location) {
        ZC_EXPECT(value == parsed.tree().node(breaks[0]).range.getStart().getAdvancedLoc(11));
      }
    }
  }
}

ZC_TEST("ParsedModule.RejectsInvalidRetainedTokenAndSourceQueries") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { outer: while (true) { break outer; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  ZC_REQUIRE(fixture.parsed != zc::none);
  ZC_IF_SOME(parsed, fixture.parsed) {
    const auto breaks = nodesOfKind(parsed.tree(), ast::SyntaxKind::BreakStmt);
    ZC_REQUIRE(breaks.size() == 1);
    ZC_EXPECT(parsed.retainedTokenSpan(breaks[0], 1, ast::SyntaxKind::BreakKeyword) == zc::none);
    ZC_EXPECT(parsed.retainedTokenSpan(breaks[0], UINT32_MAX, ast::SyntaxKind::Identifier) ==
              zc::none);
    ZC_EXPECT(parsed.retainedTokenSpan(ast::NodeId(), 0, ast::SyntaxKind::Identifier) == zc::none);

    auto alternateSnapshot =
        identity::ImmutableSourceSnapshot::from(alternateSource(), zc::heapArray("x"_zcb));
    ZC_REQUIRE(alternateSnapshot != zc::none);
    ZC_IF_SOME(snapshot, alternateSnapshot) {
      auto alternateSpan = snapshot.span(0, 1);
      ZC_REQUIRE(alternateSpan != zc::none);
      ZC_IF_SOME(span, alternateSpan) { ZC_EXPECT(parsed.sourceLocFor(span) == zc::none); }
    }
  }
}

ZC_TEST("ParsedModule.RejectsIdentifierPrefixesAsKeywordProvenance") {
  ParsedSource sourceFixture("module root;\nfun run() { breakfast; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  ZC_REQUIRE(fixture.parsed != zc::none);
  ZC_IF_SOME(parsed, fixture.parsed) {
    const auto identifiers = nodesOfKind(parsed.tree(), ast::SyntaxKind::IdentExpr);
    ZC_REQUIRE(identifiers.size() == 1);
    ZC_EXPECT(parsed.retainedTokenSpan(identifiers[0], 0, ast::SyntaxKind::BreakKeyword) ==
              zc::none);
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
    auto tokens = parseTokenSnapshot(sources, buffer);
    auto result = ParsedModuleVerifier::admit(snapshotValue, sources, buffer, zc::mv(tokens),
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
    auto firstTokens = parseTokenSnapshot(sources, buffer);
    auto secondTokens = parseTokenSnapshot(sources, buffer);
    auto first = ParsedModuleVerifier::admit(snapshotValue, sources, buffer, zc::mv(firstTokens),
                                             manualModuleTree(bufferRange, "root"_zc));
    auto second = ParsedModuleVerifier::admit(snapshotValue, sources, buffer, zc::mv(secondTokens),
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

ZC_TEST("ControlTransfer.TargetsEveryLoopForm") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  while (true) { break; continue; }\n"
      "  for (;;) { break; continue; }\n"
      "  for (let item in 0) { break; continue; }\n"
      "  do { break; continue; } while (true);\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(breaks.size() == 4);
  ZC_REQUIRE(continues.size() == 4);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& output = verified.get<VerifiedBindingOutput>();
  const auto facts = output.metadata.controlTransfers();
  ZC_REQUIRE(facts.size() == 8);
  for (size_t index = 1; index < facts.size(); ++index) {
    ZC_EXPECT(facts[index - 1].node.value < facts[index].node.value);
  }
  for (size_t index = 0; index < breaks.size(); ++index) {
    const auto& breakFact = requireControlTransfer(facts, breaks[index]);
    const auto& continueFact = requireControlTransfer(facts, continues[index]);
    ZC_EXPECT(breakFact.kind == ControlTransferKind::Break);
    ZC_EXPECT(continueFact.kind == ControlTransferKind::Continue);
    ZC_REQUIRE(breakFact.target.is<LoopControlTarget>());
    ZC_REQUIRE(continueFact.target.is<LoopControlTarget>());
    const auto breakScope = breakFact.target.get<LoopControlTarget>().scope;
    const auto continueScope = continueFact.target.get<LoopControlTarget>().scope;
    ZC_EXPECT(breakScope == continueScope);
    ZC_REQUIRE(breakScope.index() < output.metadata.scopes().size());
    ZC_EXPECT(output.metadata.scopes()[breakScope.index()].kind == ScopeKind::Loop);
    auto breakSource = input.parsedModule().spanFor(input.tree().node(breaks[index]).range);
    auto continueSource = input.parsedModule().spanFor(input.tree().node(continues[index]).range);
    ZC_REQUIRE(breakSource != zc::none);
    ZC_REQUIRE(continueSource != zc::none);
    ZC_IF_SOME(source, breakSource) { ZC_EXPECT(sameSpan(breakFact.source, source)); }
    ZC_IF_SOME(source, continueSource) { ZC_EXPECT(sameSpan(continueFact.source, source)); }
    for (size_t previous = 0; previous < index; ++previous) {
      const auto& previousFact = requireControlTransfer(facts, breaks[previous]);
      ZC_REQUIRE(previousFact.target.is<LoopControlTarget>());
      ZC_EXPECT(previousFact.target.get<LoopControlTarget>().scope != breakScope);
    }
  }
}

ZC_TEST("ControlTransfer.IncludesCurrentLoopScopeWithoutBlockBody") {
  ParsedSource sourceFixture("module root;\nfun run() { while (true) break; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  ZC_REQUIRE(breaks.size() == 1);
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireControlTransfer(metadata.controlTransfers(), breaks[0]);
  ZC_REQUIRE(fact.target.is<LoopControlTarget>());
  const auto scope = fact.target.get<LoopControlTarget>().scope;
  ZC_EXPECT(metadata.scopes()[scope.index()].kind == ScopeKind::Loop);
}

ZC_TEST("ControlTransfer.BreakTargetsMatchWhileContinueSkipsMatch") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  while (true) {\n"
      "    match (true) { default => { break; continue; } }\n"
      "  }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(breaks.size() == 1);
  ZC_REQUIRE(continues.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& breakFact = requireControlTransfer(metadata.controlTransfers(), breaks[0]);
  const auto& continueFact = requireControlTransfer(metadata.controlTransfers(), continues[0]);
  ZC_REQUIRE(breakFact.target.is<MatchControlTarget>());
  ZC_REQUIRE(continueFact.target.is<LoopControlTarget>());
  const auto matchScope = breakFact.target.get<MatchControlTarget>().scope;
  const auto loopScope = continueFact.target.get<LoopControlTarget>().scope;
  ZC_EXPECT(matchScope != loopScope);
  ZC_EXPECT(metadata.scopes()[matchScope.index()].kind == ScopeKind::Match);
  ZC_EXPECT(metadata.scopes()[loopScope.index()].kind == ScopeKind::Loop);
}

ZC_TEST("ControlTransfer.SelectsInnerLoopInsideMatch") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  match (true) {\n"
      "    default => { while (true) { break; continue; } }\n"
      "  }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(breaks.size() == 1);
  ZC_REQUIRE(continues.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& breakFact = requireControlTransfer(metadata.controlTransfers(), breaks[0]);
  const auto& continueFact = requireControlTransfer(metadata.controlTransfers(), continues[0]);
  ZC_REQUIRE(breakFact.target.is<LoopControlTarget>());
  ZC_REQUIRE(continueFact.target.is<LoopControlTarget>());
  const auto loopScope = breakFact.target.get<LoopControlTarget>().scope;
  ZC_EXPECT(continueFact.target.get<LoopControlTarget>().scope == loopScope);
  ZC_EXPECT(metadata.scopes()[loopScope.index()].kind == ScopeKind::Loop);
}

ZC_TEST("ControlTransfer.StopsAtFunctionAndClosureBoundaries") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { break; continue; }\n"
      "fun outer() {\n"
      "  while (true) {\n"
      "    let closure = fun() { break; continue; };\n"
      "  }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(breaks.size() == 2);
  ZC_REQUIRE(continues.size() == 2);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_EXPECT(value.controlTransfers.empty());
  ZC_REQUIRE(value.sourceFailures.size() == 4);
  for (const auto& failureFact : value.sourceFailures) {
    ZC_EXPECT((failureFact.emitterOrdinal >> 56) ==
              static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
    ZC_EXPECT(failureFact.notes.empty());
  }
  for (const auto node : breaks) {
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), node);
    ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
    const auto index = resolution.value.get<FailedBindingResolution>().failureIndex;
    ZC_REQUIRE(index < value.sourceFailures.size());
    ZC_EXPECT(value.sourceFailures[index].diagnostic == BinderDiagnosticCode::BreakTargetNotFound);
  }
  for (const auto node : continues) {
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), node);
    ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
    const auto index = resolution.value.get<FailedBindingResolution>().failureIndex;
    ZC_REQUIRE(index < value.sourceFailures.size());
    ZC_EXPECT(value.sourceFailures[index].diagnostic ==
              BinderDiagnosticCode::ContinueTargetNotFound);
  }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 4);
}

ZC_TEST("ControlTransfer.OrdersExactKeywordFailures") {
  class Capture final : public diagnostics::DiagnosticConsumer {
  public:
    zc::Vector<diagnostics::DiagID> ids;
    zc::Vector<source::SourceLoc> locations;

    void handleDiagnostic(const source::SourceManager&,
                          const diagnostics::Diagnostic& diagnostic) override {
      ids.add(diagnostic.getId());
      locations.add(diagnostic.getLoc());
      ZC_EXPECT(diagnostic.getArgs().size() == 0);
    }
  };

  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { cont\\u0069nue; br\\u0065ak; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(breaks.size() == 1);
  ZC_REQUIRE(continues.size() == 1);
  auto consumer = zc::heap<Capture>();
  const auto& capture = *consumer;
  sourceFixture.diagnostics->addConsumer(zc::mv(consumer));

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 2);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::ContinueTargetNotFound);
  ZC_EXPECT(value.sourceFailures[1].diagnostic == BinderDiagnosticCode::BreakTargetNotFound);
  ZC_EXPECT(value.sourceFailures[0].primary.byteEnd() -
                value.sourceFailures[0].primary.byteStart() ==
            13);
  ZC_EXPECT(value.sourceFailures[1].primary.byteEnd() -
                value.sourceFailures[1].primary.byteStart() ==
            10);
  for (const auto& failureFact : value.sourceFailures) {
    ZC_EXPECT(failureFact.notes.empty());
    ZC_EXPECT((failureFact.emitterOrdinal >> 56) ==
              static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
    ZC_EXPECT(static_cast<uint16_t>(failureFact.emitterOrdinal) == 0);
  }
  const auto& continueResolution = requireResolution(value.nodeBindings.asPtr(), continues[0]);
  const auto& breakResolution = requireResolution(value.nodeBindings.asPtr(), breaks[0]);
  ZC_REQUIRE(continueResolution.value.is<FailedBindingResolution>());
  ZC_REQUIRE(breakResolution.value.is<FailedBindingResolution>());
  ZC_EXPECT(continueResolution.value.get<FailedBindingResolution>().failureIndex == 0);
  ZC_EXPECT(breakResolution.value.get<FailedBindingResolution>().failureIndex == 1);
  ZC_REQUIRE(capture.ids.size() == 2);
  ZC_REQUIRE(capture.locations.size() == 2);
  ZC_EXPECT(capture.ids[0] == diagnostics::DiagID::ContinueTargetNotFound);
  ZC_EXPECT(capture.ids[1] == diagnostics::DiagID::BreakTargetNotFound);
  ZC_EXPECT(capture.locations[0] == input.tree().node(continues[0]).range.getStart());
  ZC_EXPECT(capture.locations[1] == input.tree().node(breaks[0]).range.getStart());

  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_REQUIRE(rejected.get<SourceRejected>().failures().size() == 2);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 2);
}

ZC_TEST("ControlTransfer.MergesFailuresWithLexicalSourceOrder") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { continue; missing; break; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 3);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::ContinueTargetNotFound);
  ZC_EXPECT(value.sourceFailures[1].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT(value.sourceFailures[2].diagnostic == BinderDiagnosticCode::BreakTargetNotFound);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 3);
}

ZC_TEST("ControlTransfer.FailsClosedForExplicitLabels") {
  const zc::StringPtr sources[] = {
      "module root;\nfun run() { outer: while (true) { break; } }\n"_zc,
      "module root;\nfun run() { while (true) { break outer; } }\n"_zc,
      "module root;\nfun run() { while (true) { continue outer; } }\n"_zc,
  };
  for (const auto sourceText : sources) {
    ParsedSource sourceFixture(sourceText);
    FrozenFixture fixture(sourceFixture, true);
    auto inputResult = verify(fixture);
    ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
    auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
    auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
    ZC_REQUIRE(candidate.is<BinderInvariantFact>());
    const auto& fact = candidate.get<BinderInvariantFact>();
    ZC_EXPECT(fact.kind == BinderInvariantKind::MissingRequiredResolution);
    ZC_EXPECT(fact.emitterSite == BinderEmitterSite::LabelAndClosure);
    ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
  }
}

ZC_TEST("BindingVerifier.RejectsMissingAdditionalAndReorderedControlTransfers") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { while (true) { break; continue; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missing.controlTransfers.size() == 2);
  missing.controlTransfers.removeLast();
  auto missingResult = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  auto& additional = additionalCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(additional.controlTransfers.size() == 2);
  const auto& duplicated = additional.controlTransfers[0];
  additional.controlTransfers.add(ControlTransferFact{duplicated.node, duplicated.kind,
                                                      cloneControlTarget(duplicated.target),
                                                      duplicated.source.clone()});
  auto additionalResult = BindingVerifier::verify(input, zc::mv(additional));
  ZC_EXPECT(requireBinderInvariant(additionalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(reorderedCandidate.is<BindingMetadataCandidate>());
  auto& reordered = reorderedCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(reordered.controlTransfers.size() == 2);
  auto first = zc::mv(reordered.controlTransfers[0]);
  reordered.controlTransfers[0] = zc::mv(reordered.controlTransfers[1]);
  reordered.controlTransfers[1] = zc::mv(first);
  auto reorderedResult = BindingVerifier::verify(input, zc::mv(reordered));
  ZC_EXPECT(requireBinderInvariant(reorderedResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsInvalidControlTargetsAndSources") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  while (true) {\n"
      "    while (true) { break; continue; }\n"
      "    break; continue;\n"
      "  }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  ZC_REQUIRE(breaks.size() == 2);

  auto kindCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(kindCandidate.is<BindingMetadataCandidate>());
  auto& wrongKind = kindCandidate.get<BindingMetadataCandidate>();
  requireControlTransfer(wrongKind.controlTransfers.asPtr(), breaks[0]).kind =
      ControlTransferKind::Continue;
  auto kindResult = BindingVerifier::verify(input, zc::mv(wrongKind));
  ZC_EXPECT(requireBinderInvariant(kindResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto targetCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(targetCandidate.is<BindingMetadataCandidate>());
  auto& wrongTarget = targetCandidate.get<BindingMetadataCandidate>();
  const auto& outerBreak = requireControlTransfer(wrongTarget.controlTransfers.asPtr(), breaks[1]);
  requireControlTransfer(wrongTarget.controlTransfers.asPtr(), breaks[0]).target =
      cloneControlTarget(outerBreak.target);
  auto targetResult = BindingVerifier::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(targetResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto sourceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(sourceCandidate.is<BindingMetadataCandidate>());
  auto& wrongSource = sourceCandidate.get<BindingMetadataCandidate>();
  const auto& outerSource = requireControlTransfer(wrongSource.controlTransfers.asPtr(), breaks[1]);
  requireControlTransfer(wrongSource.controlTransfers.asPtr(), breaks[0]).source =
      outerSource.source.clone();
  auto sourceResult = BindingVerifier::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(sourceResult).kind == BinderInvariantKind::InvalidBindingFact);

  ParsedSource matchSource(
      "module root;\n"
      "fun run() { while (true) { match (true) { default => { break; continue; } } } }\n"_zc);
  FrozenFixture matchFixture(matchSource, true);
  auto matchInputResult = verify(matchFixture);
  ZC_REQUIRE(matchInputResult.is<VerifiedBindingInput>());
  auto matchInput = zc::mv(matchInputResult.get<VerifiedBindingInput>());
  const auto matchBreaks = nodesOfKind(matchInput.tree(), ast::SyntaxKind::BreakStmt);
  const auto matchContinues = nodesOfKind(matchInput.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(matchBreaks.size() == 1);
  ZC_REQUIRE(matchContinues.size() == 1);
  auto matchCandidate = BindingBuilder::build(matchInput, *matchSource.diagnostics);
  ZC_REQUIRE(matchCandidate.is<BindingMetadataCandidate>());
  auto& continueToMatch = matchCandidate.get<BindingMetadataCandidate>();
  const auto& matchBreak =
      requireControlTransfer(continueToMatch.controlTransfers.asPtr(), matchBreaks[0]);
  ZC_REQUIRE(matchBreak.target.is<MatchControlTarget>());
  requireControlTransfer(continueToMatch.controlTransfers.asPtr(), matchContinues[0]).target =
      cloneControlTarget(matchBreak.target);
  auto matchResult = BindingVerifier::verify(matchInput, zc::mv(continueToMatch));
  ZC_EXPECT(requireBinderInvariant(matchResult).kind == BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsForeignControlTargets") {
  ParsedSource localSource("module root;\nfun run() { while (true) { break; } }\n"_zc);
  ParsedSource foreignSource("module root;\nfun run() { while (true) { break; } }\n"_zc);
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
  const auto& foreignFact = foreignCandidate.get<BindingMetadataCandidate>().controlTransfers[0];

  auto localCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(localCandidate.is<BindingMetadataCandidate>());
  auto& localFact = localCandidate.get<BindingMetadataCandidate>().controlTransfers[0];
  localFact.target = cloneControlTarget(foreignFact.target);
  auto rejected =
      BindingVerifier::verify(localInput, zc::mv(localCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireIdentityInvariant(rejected).kind() ==
            identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("BindingVerifier.EnforcesControlFailureXor") {
  ParsedSource sourceFixture("module root;\nfun run() { break; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  ZC_REQUIRE(breaks.size() == 1);

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missing.nodeBindings.size() == 1);
  missing.nodeBindings.removeLast();
  auto missingResult = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto xorCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(xorCandidate.is<BindingMetadataCandidate>());
  auto& both = xorCandidate.get<BindingMetadataCandidate>();
  auto sourceSpan = input.parsedModule().spanFor(input.tree().node(breaks[0]).range);
  ZC_REQUIRE(sourceSpan != zc::none);
  ZC_IF_SOME(span, sourceSpan) {
    both.controlTransfers.add(
        ControlTransferFact{breaks[0], ControlTransferKind::Break,
                            ControlTarget(LoopControlTarget{both.scopes[0].id}), zc::mv(span)});
  }
  auto xorResult = BindingVerifier::verify(input, zc::mv(both));
  ZC_EXPECT(requireBinderInvariant(xorResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto indexCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(indexCandidate.is<BindingMetadataCandidate>());
  auto& badIndex = indexCandidate.get<BindingMetadataCandidate>();
  auto& badIndexResolution = requireResolution(badIndex.nodeBindings.asPtr(), breaks[0]);
  ZC_REQUIRE(badIndexResolution.value.is<FailedBindingResolution>());
  badIndexResolution.value.get<FailedBindingResolution>().failureIndex =
      badIndex.sourceFailures.size();
  auto indexResult = BindingVerifier::verify(input, zc::mv(badIndex));
  ZC_EXPECT(requireBinderInvariant(indexResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto diagnosticCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(diagnosticCandidate.is<BindingMetadataCandidate>());
  auto& wrongDiagnostic = diagnosticCandidate.get<BindingMetadataCandidate>();
  wrongDiagnostic.sourceFailures[0].diagnostic = BinderDiagnosticCode::UndefinedIdentifier;
  auto diagnosticResult = BindingVerifier::verify(input, zc::mv(wrongDiagnostic));
  ZC_EXPECT(requireBinderInvariant(diagnosticResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto failureCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(failureCandidate.is<BindingMetadataCandidate>());
  auto& missingFailure = failureCandidate.get<BindingMetadataCandidate>();
  missingFailure.sourceFailures.removeLast();
  auto failureResult = BindingVerifier::verify(input, zc::mv(missingFailure));
  ZC_EXPECT(requireBinderInvariant(failureResult).kind == BinderInvariantKind::InvalidBindingFact);
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

ZC_TEST("BindingBuilder.PublishesUndefinedIdentifierFailure") {
  ParsedSource sourceFixture("module root;\nfun run() { missing; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(result.is<BindingMetadataCandidate>());
  auto& candidate = result.get<BindingMetadataCandidate>();
  ZC_REQUIRE(candidate.sourceFailures.size() == 1);
  ZC_EXPECT(candidate.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_REQUIRE(candidate.nodeBindings.size() == 1);
  ZC_REQUIRE(candidate.nodeBindings[0].value.is<FailedBindingResolution>());
  ZC_EXPECT(candidate.nodeBindings[0].value.get<FailedBindingResolution>().failureIndex == 0);
  auto rejected = BindingVerifier::verify(input, zc::mv(candidate));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
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

ZC_TEST("BindingDiagnosticAdapter.EmitsTypedRedeclarationWithPreviousNote") {
  class Capture final : public diagnostics::DiagnosticConsumer {
  public:
    diagnostics::DiagID primary = diagnostics::DiagID::UndefinedIdentifier;
    diagnostics::DiagID note = diagnostics::DiagID::UndefinedIdentifier;
    zc::String argument;

    void handleDiagnostic(const source::SourceManager&,
                          const diagnostics::Diagnostic& diagnostic) override {
      primary = diagnostic.getId();
      ZC_REQUIRE(diagnostic.getArgs().size() == 1);
      argument = zc::str(diagnostic.getArgs()[0].get<zc::String>());
      ZC_REQUIRE(diagnostic.getChildDiagnostics().size() == 1);
      note = diagnostic.getChildDiagnostics()[0]->getId();
    }
  };

  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("first second"_zcb, "main.zom");
  diagnostics::DiagnosticEngine diagnostics(sources);
  auto consumer = zc::heap<Capture>();
  const auto& capture = *consumer;
  diagnostics.addConsumer(zc::mv(consumer));
  auto identifier = identity::SemanticIdentifier::fromCanonical("value"_zc);
  ZC_REQUIRE(identifier != zc::none);
  ZC_IF_SOME(value, identifier) {
    ZC_EXPECT(BindingDiagnosticAdapter::emitRedeclaration(
        diagnostics, BinderDiagnosticCode::RedeclareVariable,
        sources.getLocForBufferStart(buffer).getAdvancedLoc(6),
        sources.getLocForBufferStart(buffer), VerifiedIdentifierArgument::from(value)));
  }
  ZC_EXPECT(capture.primary == diagnostics::DiagID::RedeclareVariable);
  ZC_EXPECT(capture.note == diagnostics::DiagID::PreviousDeclarationHere);
  ZC_EXPECT(capture.argument == "value"_zc);
}

ZC_TEST("BindingSkeleton.RejectsDuplicateFunctionsAsSourceFailures") {
  ParsedSource sourceFixture("module root;\nfun value();\nfun value();\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.definitions.size() == 2);
  ZC_REQUIRE(value.scopes[0].bindings.size() == 1);
  ZC_REQUIRE(value.currentSurface.visibleEntries.size() == 1);
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::RedeclareFunction);
  ZC_REQUIRE(value.sourceFailures[0].notes.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].notes[0].diagnostic ==
            BinderDiagnosticCode::PreviousDeclarationHere);
  ZC_EXPECT((value.sourceFailures[0].emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::ModuleSkeleton));
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_REQUIRE(rejected.get<SourceRejected>().failures().size() == 1);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("BindingSkeleton.RejectsNfcEquivalentFunctionNames") {
  ParsedSource sourceFixture("module root;\nfun e\xcc\x81();\nfun \xc3\xa9();\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(candidate.get<BindingMetadataCandidate>().sourceFailures.size() == 1);
  auto rejected = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(rejected.get<SourceRejected>().failures()[0].diagnostic ==
            BinderDiagnosticCode::RedeclareFunction);
}

ZC_TEST("BindingSkeleton.UsesKindSpecificRedeclarationCodes") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun f(); fun f();\n"
      "class C {} class C {}\n"
      "interface I {} interface I {}\n"
      "enum E {} enum E {}\n"
      "alias A = i32; alias A = i32;\n"
      "struct S {} struct S {}\n"
      "class Holder { let value: i32; let value: i32; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  const BinderDiagnosticCode expected[] = {
      BinderDiagnosticCode::RedeclareFunction,  BinderDiagnosticCode::RedeclareClass,
      BinderDiagnosticCode::RedeclareInterface, BinderDiagnosticCode::RedeclareEnum,
      BinderDiagnosticCode::RedeclareTypeAlias, BinderDiagnosticCode::DuplicateIdentifier,
      BinderDiagnosticCode::RedeclareVariable,
  };
  const auto& failures = candidate.get<BindingMetadataCandidate>().sourceFailures;
  ZC_REQUIRE(failures.size() == zc::size(expected));
  for (size_t index = 0; index < zc::size(expected); ++index) {
    ZC_EXPECT(failures[index].diagnostic == expected[index]);
    ZC_REQUIRE(failures[index].notes.size() == 1);
    ZC_EXPECT(failures[index].notes[0].diagnostic == BinderDiagnosticCode::PreviousDeclarationHere);
  }
  auto rejected = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(rejected.get<SourceRejected>().failures().size() == zc::size(expected));
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

ZC_TEST("FrozenInventory.OwnsCanonicalKeyProjection") {
  ParsedSource sourceFixture(
      "module root;\n"
      "interface Trait {}\n"
      "class Target {}\n"
      "impl Trait for Target {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true, false, ImplRegistration::Exact);
  ZC_REQUIRE(fixture.frozenDefinitions != zc::none);
  ZC_IF_SOME(inventory, fixture.frozenDefinitions) {
    ZC_REQUIRE(inventory.definitions().size() >= 1);
    ZC_REQUIRE(inventory.impls().size() == 1);
    const auto expectedDefinition = inventory.definitions()[0].key.encode();
    const auto expectedImplementation = inventory.impls()[0].key.encode();
    const auto definition = inventory.definitions()[0].definition;
    const auto implementation = inventory.impls()[0].implementation;

    auto displacedRegistries = zc::mv(fixture.registries);
    ZC_REQUIRE(displacedRegistries.definitions().lookup(definition) != zc::none);
    ZC_REQUIRE(displacedRegistries.impls().lookup(implementation) != zc::none);
    ZC_IF_SOME(key, inventory.definitionKey(definition)) {
      const auto encoded = key.encode();
      ZC_EXPECT(sameEncodedBytes(encoded.asPtr(), expectedDefinition.asPtr()));
    }
    else { ZC_EXPECT(false); }
    ZC_IF_SOME(key, inventory.implKey(implementation)) {
      const auto encoded = key.encode();
      ZC_EXPECT(sameEncodedBytes(encoded.asPtr(), expectedImplementation.asPtr()));
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

ZC_TEST("BindingSkeleton.PublishesModuleAndTypeFactsInCanonicalMaps") {
  ParsedSource sourceFixture(
      "module root;\nfun zebra();\nclass Alpha { let field: i32; fun method(); }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& output = verified.get<VerifiedBindingOutput>();
  ZC_REQUIRE(output.metadata.definitions().size() == 4);
  ZC_REQUIRE(output.metadata.scopes()[0].bindings.size() == 2);
  ZC_EXPECT(output.metadata.scopes()[0].bindings[0].name.nameSpace() == Namespace::Value);
  ZC_EXPECT(output.metadata.scopes()[0].bindings[0].name.name().text() == "zebra"_zc);
  ZC_EXPECT(output.metadata.scopes()[0].bindings[1].name.nameSpace() == Namespace::Type);
  ZC_EXPECT(output.metadata.scopes()[0].bindings[1].name.name().text() == "Alpha"_zc);
  zc::Maybe<size_t> typeScopeIndex;
  for (size_t index = 0; index < output.metadata.scopes().size(); ++index) {
    if (output.metadata.scopes()[index].kind == ScopeKind::TypeBody) { typeScopeIndex = index; }
  }
  ZC_REQUIRE(typeScopeIndex != zc::none);
  ZC_IF_SOME(index, typeScopeIndex) {
    const auto& typeScope = output.metadata.scopes()[index];
    ZC_REQUIRE(typeScope.bindings.size() == 2);
    ZC_EXPECT(typeScope.bindings[0].name.name().text() == "field"_zc);
    ZC_EXPECT(typeScope.bindings[1].name.name().text() == "method"_zc);
  }
  ZC_REQUIRE(output.surface.visibleEntries().size() == 2);
  ZC_EXPECT(output.surface.visibleEntries()[0].name.name().text() == "zebra"_zc);
  ZC_EXPECT(output.surface.visibleEntries()[1].name.name().text() == "Alpha"_zc);
}

ZC_TEST("BindingActivation.PublishesImplMembersAndNamedParameters") {
  ParsedSource implSource(
      "module root;\ninterface Action { fun act(); }\nclass Target {}\n"
      "impl Action for Target { fun act(); }\n"_zc);
  FrozenFixture implFixture(implSource, true, false, ImplRegistration::Exact);
  auto implInputResult = verify(implFixture);
  ZC_REQUIRE(implInputResult.is<VerifiedBindingInput>());
  auto implInput = zc::mv(implInputResult.get<VerifiedBindingInput>());
  auto arenaResult = ScopeArenaBuilder::build(implInput);
  ZC_REQUIRE(arenaResult.is<ScopeArenaCandidate>());
  auto arena = zc::mv(arenaResult.get<ScopeArenaCandidate>());
  auto skeletonResult = BindingSkeletonBuilder::build(implInput, arena);
  ZC_REQUIRE(skeletonResult.is<DefinitionSkeletonCandidate>());
  bool foundImpl = false;
  for (const auto& scope : arena.scopes) {
    if (scope.kind != ScopeKind::ImplBody) { continue; }
    foundImpl = true;
    ZC_REQUIRE(scope.bindings.size() == 1);
    ZC_EXPECT(scope.bindings[0].name.name().text() == "act"_zc);
  }
  ZC_EXPECT(foundImpl);
  auto implCandidate = BindingBuilder::build(implInput, *implSource.diagnostics);
  ZC_REQUIRE(implCandidate.is<BindingMetadataCandidate>());
  auto implVerified =
      BindingVerifier::verify(implInput, zc::mv(implCandidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(implVerified.is<VerifiedBindingOutput>());
  const auto& implFacts = implVerified.get<VerifiedBindingOutput>().metadata.impls();
  ZC_REQUIRE(implFacts.size() == 1);
  ZC_EXPECT(implFacts[0].identity == implFixture.implId);
  ZC_REQUIRE(implFacts[0].members.size() == 1);

  ParsedSource parameterSource("module root;\nfun apply(value: i32);\n"_zc);
  FrozenFixture parameterFixture(parameterSource, true);
  auto parameterInputResult = verify(parameterFixture);
  ZC_REQUIRE(parameterInputResult.is<VerifiedBindingInput>());
  auto parameterInput = zc::mv(parameterInputResult.get<VerifiedBindingInput>());
  auto parameterArenaResult = ScopeArenaBuilder::build(parameterInput);
  ZC_REQUIRE(parameterArenaResult.is<ScopeArenaCandidate>());
  auto parameterArena = zc::mv(parameterArenaResult.get<ScopeArenaCandidate>());
  auto parameterResult = BindingSkeletonBuilder::build(parameterInput, parameterArena);
  ZC_REQUIRE(parameterResult.is<DefinitionSkeletonCandidate>());
  const auto& parameterFacts = parameterResult.get<DefinitionSkeletonCandidate>().definitions;
  ZC_REQUIRE(parameterFacts.size() == 2);
  size_t parameterIndex = parameterFacts.size();
  for (size_t index = 0; index < parameterFacts.size(); ++index) {
    if (parameterFacts[index].kind == identity::DefinitionKind::Parameter) {
      parameterIndex = index;
      break;
    }
  }
  ZC_REQUIRE(parameterIndex < parameterFacts.size());
  const auto& parameterFact = parameterFacts[parameterIndex];
  ZC_EXPECT(parameterFact.activation == DefinitionActivation::ParameterList);
  ZC_EXPECT(parameterFact.nameSpace == Namespace::Value);
  const auto parameterScopeIndex = parameterFact.declaringScope.index();
  ZC_REQUIRE(parameterScopeIndex < parameterArena.scopes.size());
  ZC_EXPECT(parameterArena.scopes[parameterScopeIndex].kind == ScopeKind::Function);
  ZC_REQUIRE(parameterArena.scopes[parameterScopeIndex].bindings.size() == 1);
  ZC_EXPECT(parameterArena.scopes[parameterScopeIndex].bindings[0].name.name().text() ==
            "value"_zc);

  auto parameterCandidate = BindingBuilder::build(parameterInput, *parameterSource.diagnostics);
  ZC_REQUIRE(parameterCandidate.is<BindingMetadataCandidate>());
  auto parameterVerified = BindingVerifier::verify(
      parameterInput, zc::mv(parameterCandidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(parameterVerified.is<VerifiedBindingOutput>());
}

ZC_TEST("BindingSkeleton.PublishesEmptyMarkerImplFact") {
  ParsedSource sourceFixture(
      "module root;\n"
      "interface Shared {}\n"
      "class Target {}\n"
      "impl !Shared for Target;\n"_zc);
  FrozenFixture fixture(sourceFixture, true, false, ImplRegistration::Exact);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  ZC_REQUIRE(verified.get<VerifiedBindingOutput>().metadata.nodeBindings().size() == 2);
  for (const auto& binding : verified.get<VerifiedBindingOutput>().metadata.nodeBindings()) {
    ZC_REQUIRE(binding.value.is<BoundNameResolution>());
    ZC_EXPECT(binding.value.get<BoundNameResolution>().nameSpace == Namespace::Type);
  }
  const auto& facts = verified.get<VerifiedBindingOutput>().metadata.impls();
  ZC_REQUIRE(facts.size() == 1);
  ZC_EXPECT(facts[0].identity == fixture.implId);
  ZC_EXPECT(facts[0].members.empty());
}

ZC_TEST("BindingVerifier.RejectsMalformedImplFactsAndMemberOrder") {
  ParsedSource sourceFixture(
      "module root;\ninterface Action { fun alpha(); fun zeta(); }\nclass Target {}\n"
      "impl Action for Target { fun zeta(); fun alpha(); }\n"_zc);
  FrozenFixture fixture(sourceFixture, true, false, ImplRegistration::Exact);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());

  auto reorderedCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(reorderedCandidate.is<BindingMetadataCandidate>());
  auto& reordered = reorderedCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(reordered.impls.size() == 1);
  ZC_REQUIRE(reordered.impls[0].members.size() == 2);
  const auto first = reordered.impls[0].members[0];
  reordered.impls[0].members[0] = reordered.impls[0].members[1];
  reordered.impls[0].members[1] = first;
  auto reorderedResult = BindingVerifier::verify(input, zc::mv(reordered));
  ZC_EXPECT(requireBinderInvariant(reorderedResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  missing.impls[0].members.removeLast();
  auto missingResult = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto additionalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  auto& additional = additionalCandidate.get<BindingMetadataCandidate>();
  additional.impls[0].members.add(additional.impls[0].members[0]);
  auto additionalResult = BindingVerifier::verify(input, zc::mv(additional));
  ZC_EXPECT(requireBinderInvariant(additionalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto scopeCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(scopeCandidate.is<BindingMetadataCandidate>());
  auto& wrongScope = scopeCandidate.get<BindingMetadataCandidate>();
  wrongScope.impls[0].scope = wrongScope.scopes[0].id;
  auto scopeResult = BindingVerifier::verify(input, zc::mv(wrongScope));
  ZC_EXPECT(requireBinderInvariant(scopeResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto sourceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(sourceCandidate.is<BindingMetadataCandidate>());
  auto& wrongSource = sourceCandidate.get<BindingMetadataCandidate>();
  wrongSource.impls[0].source = wrongSource.definitions[0].source.clone();
  auto sourceResult = BindingVerifier::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(sourceResult).kind == BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingSkeleton.IncludesModuleConstantPatternLeaves") {
  ParsedSource sourceFixture("module root;\nconst (left, right) = (1, 2);\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& output = verified.get<VerifiedBindingOutput>();
  ZC_REQUIRE(output.metadata.definitions().size() == 2);
  ZC_REQUIRE(output.metadata.scopes()[0].bindings.size() == 2);
  ZC_EXPECT(output.metadata.scopes()[0].bindings[0].name.name().text() == "left"_zc);
  ZC_EXPECT(output.metadata.scopes()[0].bindings[1].name.name().text() == "right"_zc);
  for (const auto& fact : output.metadata.definitions()) {
    ZC_EXPECT(fact.kind == identity::DefinitionKind::Constant);
    ZC_EXPECT(fact.activation == DefinitionActivation::ModuleSkeleton);
    ZC_REQUIRE(fact.site.value().is<PatternBindingSite>());
    ZC_EXPECT(fact.site.value().get<PatternBindingSite>().patternPath.size() == 2);
  }
}

ZC_TEST("BindingSkeleton.PublishesOnlyDeclarationExports") {
  ParsedSource sourceFixture(
      "module root;\nexport class Point { let x: i32; }\nfun private_value();\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& surface = verified.get<VerifiedBindingOutput>().surface;
  ZC_REQUIRE(surface.visibleEntries().size() == 2);
  ZC_REQUIRE(surface.exports().size() == 1);
  ZC_EXPECT(surface.visibleEntries()[0].name.name().text() == "private_value"_zc);
  ZC_EXPECT(!surface.visibleEntries()[0].exported);
  ZC_EXPECT(surface.visibleEntries()[1].name.name().text() == "Point"_zc);
  ZC_EXPECT(surface.visibleEntries()[1].exported);
  ZC_EXPECT(surface.visibleEntries()[1].visibility.value().is<ExternalVisibility>());
  ZC_EXPECT(surface.visibleEntries()[1].exportSpan != zc::none);
  ZC_EXPECT(surface.exports()[0].name.name().text() == "Point"_zc);
  ZC_EXPECT(surface.exports()[0].exported);
}

ZC_TEST("BindingActivation.PublishesScopeOwningGenericLists") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun build<T>();\n"
      "class Box<U> { fun map<Z>(); }\n"
      "struct Pair<V> {}\n"
      "interface Action<W> {}\n"
      "enum Choice<X> { None }\n"
      "impl<Y> Action<Y> for Box<Y> {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true, false, ImplRegistration::Exact);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;

  size_t genericCount = 0;
  for (const auto& fact : metadata.definitions()) {
    if (fact.kind != identity::DefinitionKind::TypeParameter) { continue; }
    ++genericCount;
    ZC_EXPECT(fact.activation == DefinitionActivation::GenericList);
    ZC_EXPECT(fact.nameSpace == Namespace::Type);
    ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind != ScopeKind::Module);
  }
  ZC_EXPECT(genericCount == 7);
  ZC_REQUIRE(metadata.impls().size() == 1);
  ZC_EXPECT(metadata.impls()[0].members.empty());
}

ZC_TEST("BindingActivation.RejectsDuplicateGenericParameters") {
  ParsedSource sourceFixture("module root;\nfun build<T, T>();\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto result = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(result.is<SourceRejected>());
  const auto failures = result.get<SourceRejected>().failures();
  ZC_REQUIRE(failures.size() == 1);
  ZC_EXPECT(failures[0].diagnostic == BinderDiagnosticCode::DuplicateIdentifier);
  ZC_REQUIRE(failures[0].notes.size() == 1);
  ZC_EXPECT(failures[0].notes[0].diagnostic == BinderDiagnosticCode::PreviousDeclarationHere);
}

ZC_TEST("BindingActivation.PublishesNamedCallableParameterLists") {
  ParsedSource sourceFixture(
      "module root;\n"
      "extern \"C\" { fun ffi(raw: i32); }\n"
      "fun apply(first: i32, second: i32);\n"
      "class Box { fun map(item: i32); }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;

  size_t parameterCount = 0;
  for (const auto& fact : metadata.definitions()) {
    if (fact.kind != identity::DefinitionKind::Parameter) { continue; }
    ++parameterCount;
    ZC_EXPECT(fact.activation == DefinitionActivation::ParameterList);
    ZC_EXPECT(fact.nameSpace == Namespace::Value);
    ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Function);
  }
  ZC_EXPECT(parameterCount == 4);
}

ZC_TEST("BindingActivation.RejectsDuplicateNamedParameters") {
  ParsedSource sourceFixture("module root;\nfun apply(value: i32, value: i32);\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto result = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(result.is<SourceRejected>());
  const auto failures = result.get<SourceRejected>().failures();
  ZC_REQUIRE(failures.size() == 1);
  ZC_EXPECT(failures[0].diagnostic == BinderDiagnosticCode::RedeclareParameter);
  ZC_REQUIRE(failures[0].notes.size() == 1);
  ZC_EXPECT(failures[0].notes[0].diagnostic == BinderDiagnosticCode::PreviousDeclarationHere);
}

ZC_TEST("BindingActivation.PublishesSpecialCallableParameterLists") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Box {\n"
      "  init(value: i32) {}\n"
      "  deinit(token: i32) {}\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  size_t specialIdentityCount = 0;
  for (const auto& entry : input.definitions().definitions()) {
    if (entry.kind != identity::DefinitionKind::Constructor &&
        entry.kind != identity::DefinitionKind::Destructor) {
      continue;
    }
    ++specialIdentityCount;
    ZC_EXPECT(entry.bindingName == zc::none);
  }
  ZC_EXPECT(specialIdentityCount == 2);
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;

  size_t constructorCount = 0;
  size_t destructorCount = 0;
  size_t parameterCount = 0;
  size_t specialCallableScopeCount = 0;
  for (const auto& fact : metadata.definitions()) {
    if (fact.kind == identity::DefinitionKind::Constructor) {
      ++constructorCount;
      ZC_EXPECT(fact.activation == DefinitionActivation::ModuleSkeleton);
      ZC_EXPECT(fact.nameSpace == Namespace::Value);
    }
    if (fact.kind == identity::DefinitionKind::Destructor) {
      ++destructorCount;
      ZC_EXPECT(fact.activation == DefinitionActivation::ModuleSkeleton);
      ZC_EXPECT(fact.nameSpace == Namespace::Value);
    }
    if (fact.kind == identity::DefinitionKind::Parameter) {
      ++parameterCount;
      ZC_EXPECT(fact.activation == DefinitionActivation::ParameterList);
      ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Function);
    }
  }
  for (const auto& scope : metadata.scopes()) {
    if (scope.kind != ScopeKind::Function) { continue; }
    ++specialCallableScopeCount;
    ZC_REQUIRE(scope.bindings.size() == 1);
  }
  ZC_EXPECT(constructorCount == 1);
  ZC_EXPECT(destructorCount == 1);
  ZC_EXPECT(parameterCount == 2);
  ZC_EXPECT(specialCallableScopeCount == 2);
  ZC_REQUIRE(metadata.scopes().size() > 1);
  ZC_EXPECT(metadata.scopes()[1].kind == ScopeKind::TypeBody);
  ZC_EXPECT(metadata.scopes()[1].bindings.empty());
}

ZC_TEST("BindingActivation.PublishesClosureIdentityAndParameters") {
  ParsedSource sourceFixture(
      "module root;\n"
      "const transform = fun<T>(value: T) -> T {};\n"
      "const project = (item: i32) => 0;\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& output = verified.get<VerifiedBindingOutput>();
  const auto& metadata = output.metadata;

  size_t closureCount = 0;
  size_t genericCount = 0;
  size_t parameterCount = 0;
  for (const auto& fact : metadata.definitions()) {
    if (fact.kind == identity::DefinitionKind::Closure) {
      ++closureCount;
      ZC_EXPECT(fact.activation == DefinitionActivation::ExpressionIntroduction);
      ZC_EXPECT(fact.nameSpace == Namespace::Value);
      ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Module);
    }
    if (fact.kind == identity::DefinitionKind::TypeParameter) {
      ++genericCount;
      ZC_EXPECT(fact.activation == DefinitionActivation::GenericList);
      ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Closure);
    }
    if (fact.kind == identity::DefinitionKind::Parameter) {
      ++parameterCount;
      ZC_EXPECT(fact.activation == DefinitionActivation::ParameterList);
      ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Closure);
    }
  }
  ZC_EXPECT(closureCount == 2);
  ZC_EXPECT(genericCount == 1);
  ZC_EXPECT(parameterCount == 2);

  size_t closureScopeCount = 0;
  for (const auto& scope : metadata.scopes()) {
    if (scope.kind != ScopeKind::Closure) { continue; }
    ++closureScopeCount;
  }
  ZC_EXPECT(closureScopeCount == 2);
  ZC_REQUIRE(metadata.scopes()[0].bindings.size() == 2);
  ZC_EXPECT(metadata.scopes()[0].bindings[0].name.name().text() == "project"_zc);
  ZC_EXPECT(metadata.scopes()[0].bindings[1].name.name().text() == "transform"_zc);
  ZC_REQUIRE(output.surface.visibleEntries().size() == 2);
}

ZC_TEST("BindingActivation.PublishesMatchAndLoopPatternFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun scan() {\n"
      "  for (let item in [1]) {}\n"
      "  match (1) {\n"
      "    when matched => {}\n"
      "  }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& output = verified.get<VerifiedBindingOutput>();
  const auto& metadata = output.metadata;

  size_t loopPatternCount = 0;
  size_t matchPatternCount = 0;
  for (const auto& fact : metadata.definitions()) {
    if (fact.kind != identity::DefinitionKind::PatternBinding) { continue; }
    ZC_EXPECT(fact.nameSpace == Namespace::Value);
    ZC_REQUIRE(fact.site.value().is<PatternBindingSite>());
    const auto introducer = fact.site.value().get<PatternBindingSite>().introducer;
    if (fact.activation == DefinitionActivation::LoopPattern) {
      ++loopPatternCount;
      ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Loop);
      ZC_EXPECT(input.tree().node(introducer).kind == ast::SyntaxKind::ForInStatement);
    }
    if (fact.activation == DefinitionActivation::MatchPattern) {
      ++matchPatternCount;
      ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::MatchArm);
      ZC_EXPECT(input.tree().node(introducer).kind == ast::SyntaxKind::MatchArmStmt);
    }
  }
  ZC_EXPECT(loopPatternCount == 1);
  ZC_EXPECT(matchPatternCount == 1);

  size_t loopBindingCount = 0;
  size_t armBindingCount = 0;
  for (const auto& scope : metadata.scopes()) {
    if (scope.kind == ScopeKind::Loop) { loopBindingCount += scope.bindings.size(); }
    if (scope.kind == ScopeKind::MatchArm) { armBindingCount += scope.bindings.size(); }
  }
  ZC_EXPECT(loopBindingCount == 1);
  ZC_EXPECT(armBindingCount == 1);
  ZC_REQUIRE(metadata.scopes()[0].bindings.size() == 1);
  ZC_EXPECT(metadata.scopes()[0].bindings[0].name.name().text() == "scan"_zc);
  ZC_REQUIRE(output.surface.visibleEntries().size() == 1);
}

ZC_TEST("BindingActivation.PublishesBlockLocalsAfterInitializers") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun build() {\n"
      "  let [first, second] = [1, 2];\n"
      "  mut third = 3;\n"
      "  const fourth = 4;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& output = verified.get<VerifiedBindingOutput>();
  const auto& metadata = output.metadata;

  size_t localCount = 0;
  for (const auto& fact : metadata.definitions()) {
    if (fact.kind != identity::DefinitionKind::Local) { continue; }
    ++localCount;
    ZC_EXPECT(fact.activation == DefinitionActivation::AfterInitializer);
    ZC_EXPECT(fact.nameSpace == Namespace::Value);
    ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Block);
    ZC_REQUIRE(fact.site.value().is<PatternBindingSite>());
    const auto introducer = fact.site.value().get<PatternBindingSite>().introducer;
    ZC_EXPECT(input.tree().node(introducer).kind == ast::SyntaxKind::VariableDeclarator);
  }
  ZC_EXPECT(localCount == 4);

  size_t localBlockCount = 0;
  for (const auto& scope : metadata.scopes()) {
    if (scope.kind != ScopeKind::Block || scope.bindings.size() != 4) { continue; }
    ++localBlockCount;
    ZC_EXPECT(scope.bindings[0].name.name().text() == "first"_zc);
    ZC_EXPECT(scope.bindings[1].name.name().text() == "fourth"_zc);
    ZC_EXPECT(scope.bindings[2].name.name().text() == "second"_zc);
    ZC_EXPECT(scope.bindings[3].name.name().text() == "third"_zc);
  }
  ZC_EXPECT(localBlockCount == 1);
  ZC_REQUIRE(metadata.scopes()[0].bindings.size() == 1);
  ZC_EXPECT(metadata.scopes()[0].bindings[0].name.name().text() == "build"_zc);
  ZC_REQUIRE(output.surface.visibleEntries().size() == 1);
}

ZC_TEST("BodyBinding.ResolvesEarlierDeclaratorsAfterActivation") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun build() {\n"
      "  let first = 1, second = first;\n"
      "  return second;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto firstDefinition = requireScopeDefinition(metadata.scopes(), "first"_zc);
  const auto secondDefinition = requireScopeDefinition(metadata.scopes(), "second"_zc);
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  const auto secondReferences = identifierExpressions(input.tree(), "second"_zc);
  ZC_REQUIRE(firstReferences.size() == 1);
  ZC_REQUIRE(secondReferences.size() == 1);
  const auto& firstResolution = requireResolution(metadata.nodeBindings(), firstReferences[0]);
  const auto& secondResolution = requireResolution(metadata.nodeBindings(), secondReferences[0]);
  ZC_REQUIRE(firstResolution.value.is<BoundNameResolution>());
  ZC_REQUIRE(secondResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(
      requireDefinitionTarget(firstResolution.value.get<BoundNameResolution>().bindingIdentity) ==
      firstDefinition);
  ZC_EXPECT(
      requireDefinitionTarget(secondResolution.value.get<BoundNameResolution>().bindingIdentity) ==
      secondDefinition);
}

ZC_TEST("BodyBinding.RejectsSelfReferenceBeforeActivation") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun build() {\n"
      "  let value = value;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT((value.sourceFailures[0].emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
  ZC_EXPECT(value.sourceFailures[0].notes.empty());
  const auto references = identifierExpressions(input.tree(), "value"_zc);
  ZC_REQUIRE(references.size() == 1);
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), references[0]);
  ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
  ZC_EXPECT(resolution.value.get<FailedBindingResolution>().failureIndex == 0);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("BodyBinding.RejectsLaterDeclaratorReference") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun build() {\n"
      "  let first = second, second = 2;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  const auto references = identifierExpressions(input.tree(), "second"_zc);
  ZC_REQUIRE(references.size() == 1);
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), references[0]);
  ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
  ZC_EXPECT(resolution.value.get<FailedBindingResolution>().failureIndex == 0);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.RecordsOuterShadowTargetAndResolvesNearestBinding") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun build() {\n"
      "  let value = 1;\n"
      "  {\n"
      "    let value = value;\n"
      "    value;\n"
      "  }\n"
      "  value;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto outerDefinition = requireScopeDefinition(metadata.scopes(), "value"_zc, 0);
  const auto innerDefinition = requireScopeDefinition(metadata.scopes(), "value"_zc, 1);
  ZC_REQUIRE(metadata.shadowTargets().size() == 1);
  ZC_EXPECT(metadata.shadowTargets()[0].definition == innerDefinition);
  ZC_EXPECT(requireDefinitionTarget(metadata.shadowTargets()[0].target) == outerDefinition);
  const auto references = identifierExpressions(input.tree(), "value"_zc);
  ZC_REQUIRE(references.size() == 3);
  const auto& initializerResolution = requireResolution(metadata.nodeBindings(), references[0]);
  const auto& innerResolution = requireResolution(metadata.nodeBindings(), references[1]);
  const auto& outerResolution = requireResolution(metadata.nodeBindings(), references[2]);
  ZC_REQUIRE(initializerResolution.value.is<BoundNameResolution>());
  ZC_REQUIRE(innerResolution.value.is<BoundNameResolution>());
  ZC_REQUIRE(outerResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                initializerResolution.value.get<BoundNameResolution>().bindingIdentity) ==
            outerDefinition);
  ZC_EXPECT(
      requireDefinitionTarget(innerResolution.value.get<BoundNameResolution>().bindingIdentity) ==
      innerDefinition);
  ZC_EXPECT(
      requireDefinitionTarget(outerResolution.value.get<BoundNameResolution>().bindingIdentity) ==
      outerDefinition);
}

ZC_TEST("BodyBinding.ActivatesForInPatternAfterIterable") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  let item = 1;\n"
      "  for (let item in [item]) { item; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto outerDefinition = requireScopeDefinition(metadata.scopes(), "item"_zc, 0);
  const auto loopDefinition = requireScopeDefinition(metadata.scopes(), "item"_zc, 1);
  const auto references = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(references.size() == 2);
  const auto& iterableResolution = requireResolution(metadata.nodeBindings(), references[0]);
  const auto& bodyResolution = requireResolution(metadata.nodeBindings(), references[1]);
  ZC_REQUIRE(iterableResolution.value.is<BoundNameResolution>());
  ZC_REQUIRE(bodyResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                iterableResolution.value.get<BoundNameResolution>().bindingIdentity) ==
            outerDefinition);
  ZC_EXPECT(requireDefinitionTarget(
                bodyResolution.value.get<BoundNameResolution>().bindingIdentity) == loopDefinition);
  ZC_REQUIRE(metadata.shadowTargets().size() == 1);
  ZC_EXPECT(metadata.shadowTargets()[0].definition == loopDefinition);
  ZC_EXPECT(requireDefinitionTarget(metadata.shadowTargets()[0].target) == outerDefinition);
}

ZC_TEST("BodyBinding.ActivatesMatchPatternForGuardAndBody") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  let item = 1;\n"
      "  match (item) {\n"
      "    when item if item > 0 => { item; }\n"
      "  }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto outerDefinition = requireScopeDefinition(metadata.scopes(), "item"_zc, 0);
  const auto armDefinition = requireScopeDefinition(metadata.scopes(), "item"_zc, 1);
  const auto references = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(references.size() == 3);
  const auto& scrutineeResolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(scrutineeResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                scrutineeResolution.value.get<BoundNameResolution>().bindingIdentity) ==
            outerDefinition);
  for (size_t index = 1; index < references.size(); ++index) {
    const auto& resolution = requireResolution(metadata.nodeBindings(), references[index]);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    ZC_EXPECT(requireDefinitionTarget(
                  resolution.value.get<BoundNameResolution>().bindingIdentity) == armDefinition);
  }
  ZC_REQUIRE(metadata.shadowTargets().size() == 1);
  ZC_EXPECT(metadata.shadowTargets()[0].definition == armDefinition);
  ZC_EXPECT(requireDefinitionTarget(metadata.shadowTargets()[0].target) == outerDefinition);
}

ZC_TEST("BodyBinding.OrdersParameterDefaultVisibilityBySource") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(first: i32 = 1, second: i32 = first) { second; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto firstDefinition = requireScopeDefinition(metadata.scopes(), "first"_zc);
  const auto secondDefinition = requireScopeDefinition(metadata.scopes(), "second"_zc);
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  const auto secondReferences = identifierExpressions(input.tree(), "second"_zc);
  ZC_REQUIRE(firstReferences.size() == 1);
  ZC_REQUIRE(secondReferences.size() == 1);
  ZC_EXPECT(requireDefinitionTarget(requireResolution(metadata.nodeBindings(), firstReferences[0])
                                        .value.get<BoundNameResolution>()
                                        .bindingIdentity) == firstDefinition);
  ZC_EXPECT(requireDefinitionTarget(requireResolution(metadata.nodeBindings(), secondReferences[0])
                                        .value.get<BoundNameResolution>()
                                        .bindingIdentity) == secondDefinition);
}

ZC_TEST("BodyBinding.RejectsLaterParameterInEarlierDefault") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(first: i32 = second, second: i32 = 1) {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_REQUIRE(value.nodeBindings.size() == 1);
  ZC_REQUIRE(value.nodeBindings[0].value.is<FailedBindingResolution>());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.ReportsValueUseOfTypeNameAsNamespaceMismatch") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Value {}\n"
      "fun run() { Value; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::SymbolNamespaceMismatch);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.ResolvesNamedTypeReferences") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Value {}\n"
      "fun run(value: Value) {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.empty());
  ZC_REQUIRE(value.nodeBindings.size() == 1);
  ZC_REQUIRE(value.nodeBindings[0].value.is<BoundNameResolution>());
  const auto& resolution = value.nodeBindings[0].value.get<BoundNameResolution>();
  ZC_EXPECT(resolution.nameSpace == Namespace::Type);
  ZC_EXPECT(requireDefinitionTarget(resolution.bindingIdentity) ==
            requireScopeDefinitionInNamespace(value.scopes.asPtr(), "Value"_zc, Namespace::Type));
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
}

ZC_TEST("BodyBinding.ResolvesTypeQueryPathsInValueNamespace") {
  ParsedSource sourceFixture(
      "module root;\n"
      "let value: i32 = 1;\n"
      "let result: typeof value = value;\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.empty());
  const auto localDefinition =
      requireScopeDefinitionInNamespace(value.scopes.asPtr(), "value"_zc, Namespace::Value);
  const auto paths = identifierPaths(input.tree(), ast::SyntaxKind::ModulePath, "value"_zc);
  ZC_REQUIRE(paths.size() == 1);
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), paths[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  ZC_EXPECT(resolution.value.get<BoundNameResolution>().nameSpace == Namespace::Value);
  ZC_EXPECT(requireDefinitionTarget(resolution.value.get<BoundNameResolution>().bindingIdentity) ==
            localDefinition);
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
}

ZC_TEST("BodyBinding.ReportsTypeQueryTypeOnlyNamespaceMismatch") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Value {}\n"
      "let result: typeof Value = 1;\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::SymbolNamespaceMismatch);
  ZC_REQUIRE(value.nodeBindings.size() == 1);
  ZC_REQUIRE(value.nodeBindings[0].value.is<FailedBindingResolution>());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.RejectsUnverifiedQualifiedTypeQueryPaths") {
  ParsedSource sourceFixture(
      "module root;\n"
      "let value: i32 = 1;\n"
      "let result: typeof value.field = 1;\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BinderInvariantFact>());
  ZC_EXPECT(candidate.get<BinderInvariantFact>().kind ==
            BinderInvariantKind::MissingRequiredResolution);
  ZC_EXPECT(candidate.get<BinderInvariantFact>().emitterSite == BinderEmitterSite::BodyBinding);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BodyBinding.ResolvesDynMarkerPathsInTypeNamespace") {
  ParsedSource sourceFixture(
      "module root;\n"
      "interface Read {}\n"
      "interface Sendable {}\n"
      "alias Handler = dyn Read + Sendable;\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.empty());
  const auto markerPaths =
      identifierPaths(input.tree(), ast::SyntaxKind::AttributePath, "Sendable"_zc);
  ZC_REQUIRE(markerPaths.size() == 1);
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), markerPaths[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  ZC_EXPECT(resolution.value.get<BoundNameResolution>().nameSpace == Namespace::Type);
  ZC_EXPECT(
      requireDefinitionTarget(resolution.value.get<BoundNameResolution>().bindingIdentity) ==
      requireScopeDefinitionInNamespace(value.scopes.asPtr(), "Sendable"_zc, Namespace::Type));
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
}

ZC_TEST("BodyBinding.RejectsUndefinedDynMarkerPaths") {
  ParsedSource sourceFixture(
      "module root;\n"
      "interface Read {}\n"
      "alias Handler = dyn Read + Missing;\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  const auto paths = identifierPaths(input.tree(), ast::SyntaxKind::AttributePath, "Missing"_zc);
  ZC_REQUIRE(paths.size() == 1);
  ZC_REQUIRE(
      requireResolution(value.nodeBindings.asPtr(), paths[0]).value.is<FailedBindingResolution>());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.RejectsUnverifiedQualifiedDynMarkerPaths") {
  ParsedSource sourceFixture(
      "module root;\n"
      "interface Read {}\n"
      "alias Handler = dyn Read + marker::Sendable;\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BinderInvariantFact>());
  ZC_EXPECT(candidate.get<BinderInvariantFact>().kind ==
            BinderInvariantKind::MissingRequiredResolution);
  ZC_EXPECT(candidate.get<BinderInvariantFact>().emitterSite == BinderEmitterSite::BodyBinding);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BodyBinding.ResolvesObjectShorthandAndSkipsExplicitKeys") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { let value = 1; let record = { value, key: value }; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.empty());
  ZC_REQUIRE(value.nodeBindings.size() == 2);
  const auto definition = requireScopeDefinition(value.scopes.asPtr(), "value"_zc);
  const auto shorthand = shorthandProperties(input.tree(), "value"_zc);
  const auto explicitValues = identifierExpressions(input.tree(), "value"_zc);
  ZC_REQUIRE(shorthand.size() == 1);
  ZC_REQUIRE(explicitValues.size() == 1);
  for (const auto node : {shorthand[0], explicitValues[0]}) {
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), node);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    ZC_EXPECT(resolution.value.get<BoundNameResolution>().nameSpace == Namespace::Value);
    ZC_EXPECT(requireDefinitionTarget(
                  resolution.value.get<BoundNameResolution>().bindingIdentity) == definition);
  }
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
}

ZC_TEST("BodyBinding.RejectsUndefinedObjectShorthand") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { let record = { missing }; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  const auto shorthand = shorthandProperties(input.tree(), "missing"_zc);
  ZC_REQUIRE(shorthand.size() == 1);
  ZC_REQUIRE(requireResolution(value.nodeBindings.asPtr(), shorthand[0])
                 .value.is<FailedBindingResolution>());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.RejectsUndefinedTypeReferences") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(value: Missing) {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_REQUIRE(value.nodeBindings.size() == 1);
  ZC_REQUIRE(value.nodeBindings[0].value.is<FailedBindingResolution>());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.KeepsParametersOutOfLaterParameterTypes") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(first: i32, second: [i32; first]) {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_REQUIRE(value.nodeBindings.size() == 1);
  ZC_REQUIRE(value.nodeBindings[0].value.is<FailedBindingResolution>());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.KeepsParametersOutOfReturnTypes") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(size: i32) -> [i32; size] {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_REQUIRE(value.nodeBindings.size() == 1);
  ZC_REQUIRE(value.nodeBindings[0].value.is<FailedBindingResolution>());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.ReportsTypeUseOfValueNameAsNamespaceMismatch") {
  ParsedSource sourceFixture(
      "module root;\n"
      "const Value = 1;\n"
      "fun run(value: Value) {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::SymbolNamespaceMismatch);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.ResolvesStructPatternTypePaths") {
  ParsedSource sourceFixture(
      "module root;\n"
      "struct Item { value: i32 }\n"
      "fun run(item: Item) {\n"
      "  match (item) { when Item { value } => { value; } }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto itemDefinition =
      requireScopeDefinitionInNamespace(metadata.scopes(), "Item"_zc, Namespace::Type);
  size_t typePathCount = 0;
  for (const auto& binding : metadata.nodeBindings()) {
    if (!binding.value.is<BoundNameResolution>()) { continue; }
    const auto& resolution = binding.value.get<BoundNameResolution>();
    if (resolution.nameSpace == Namespace::Type &&
        requireDefinitionTarget(resolution.bindingIdentity) == itemDefinition) {
      ++typePathCount;
    }
  }
  ZC_EXPECT(typePathCount == 2);
}

ZC_TEST("BodyBinding.AcceptsStructPatternsWithoutTypePaths") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(item: i32) {\n"
      "  match (item) { when { value } => { value; } }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto valueDefinition = requireScopeDefinition(metadata.scopes(), "value"_zc);
  const auto valueUses = identifierExpressions(input.tree(), "value"_zc);
  ZC_REQUIRE(valueUses.size() == 1);
  const auto& resolution = requireResolution(metadata.nodeBindings(), valueUses[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(resolution.value.get<BoundNameResolution>().bindingIdentity) ==
            valueDefinition);
}

ZC_TEST("BodyBinding.RejectsUnverifiedQualifiedTypePaths") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(value: dependency::Missing) {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BinderInvariantFact>());
  ZC_EXPECT(candidate.get<BinderInvariantFact>().kind ==
            BinderInvariantKind::MissingRequiredResolution);
  ZC_EXPECT(candidate.get<BinderInvariantFact>().emitterSite == BinderEmitterSite::BodyBinding);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BodyBinding.RejectsNfcEquivalentLocalNames") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { let e\xcc\x81 = 1; let \xc3\xa9 = 2; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::RedeclareVariable);
  ZC_EXPECT((value.sourceFailures[0].emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
  ZC_REQUIRE(value.sourceFailures[0].notes.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].notes[0].diagnostic ==
            BinderDiagnosticCode::PreviousDeclarationHere);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.OrdersLookupAndDuplicateFailuresBySource") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  missingFirst;\n"
      "  let value = missingSecond;\n"
      "  let value = 1;\n"
      "  missingThird;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 4);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT(value.sourceFailures[1].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT(value.sourceFailures[2].diagnostic == BinderDiagnosticCode::RedeclareVariable);
  ZC_EXPECT(value.sourceFailures[3].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  for (size_t index = 1; index < value.sourceFailures.size(); ++index) {
    ZC_EXPECT(value.sourceFailures[index - 1].primary.byteStart() <
              value.sourceFailures[index].primary.byteStart());
  }
  for (const auto& expected : {"missingFirst"_zc, "missingSecond"_zc, "missingThird"_zc}) {
    const auto references = identifierExpressions(input.tree(), expected);
    ZC_REQUIRE(references.size() == 1);
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), references[0]);
    ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
    const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
    ZC_REQUIRE(failureIndex < value.sourceFailures.size());
    ZC_EXPECT(value.sourceFailures[failureIndex].diagnostic ==
              BinderDiagnosticCode::UndefinedIdentifier);
    auto source = input.parsedModule().spanFor(input.tree().node(references[0]).range);
    ZC_REQUIRE(source != zc::none);
    ZC_IF_SOME(span, source) {
      ZC_EXPECT(value.sourceFailures[failureIndex].primary.byteStart() == span.byteStart());
    }
  }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BodyBinding.CanonicalizesSkeletonAndBodyDefinitionFactsTogether") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun alpha<T>(parameter: T) { let local = parameter; }\n"
      "fun beta() { let nested = 1; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.definitions.size() >= 6);
  for (size_t index = 1; index < value.definitions.size(); ++index) {
    const auto previous =
        requireFrozenDefinition(input, value.definitions[index - 1].identity).key.encode();
    const auto current =
        requireFrozenDefinition(input, value.definitions[index].identity).key.encode();
    ZC_EXPECT(encodedBytesLess(previous.asPtr(), current.asPtr()));
  }
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
}

ZC_TEST("BindingVerifier.RejectsReorderedCombinedDefinitionFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun alpha() { let local = 1; }\n"
      "fun beta() {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.definitions.size() >= 2);
  auto displaced = zc::mv(value.definitions[0]);
  value.definitions[0] = zc::mv(value.definitions[1]);
  value.definitions[1] = zc::mv(displaced);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_EXPECT(requireBinderInvariant(rejected).kind == BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsWrongBoundNameTarget") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { let first = 1, second = first; second; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  const auto secondDefinition = requireScopeDefinition(value.scopes.asPtr(), "second"_zc);
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  ZC_REQUIRE(firstReferences.size() == 1);
  auto& resolution = requireResolution(value.nodeBindings.asPtr(), firstReferences[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  resolution.value.get<BoundNameResolution>().bindingIdentity =
      BindingTarget::definition(secondDefinition);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_EXPECT(requireBinderInvariant(rejected).kind == BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsMalformedBoundNameFieldsAndOrder") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { let first = 1, second = first; second; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  ZC_REQUIRE(firstReferences.size() == 1);

  auto canonicalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(canonicalCandidate.is<BindingMetadataCandidate>());
  auto& wrongCanonical = canonicalCandidate.get<BindingMetadataCandidate>();
  const auto secondDefinition = requireScopeDefinition(wrongCanonical.scopes.asPtr(), "second"_zc);
  auto& canonicalResolution =
      requireResolution(wrongCanonical.nodeBindings.asPtr(), firstReferences[0]);
  ZC_REQUIRE(canonicalResolution.value.is<BoundNameResolution>());
  canonicalResolution.value.get<BoundNameResolution>().canonicalTarget =
      BindingTarget::definition(secondDefinition);
  auto canonicalRejected = BindingVerifier::verify(input, zc::mv(wrongCanonical));
  ZC_EXPECT(requireBinderInvariant(canonicalRejected).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto namespaceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(namespaceCandidate.is<BindingMetadataCandidate>());
  auto& wrongNamespace = namespaceCandidate.get<BindingMetadataCandidate>();
  auto& namespaceResolution =
      requireResolution(wrongNamespace.nodeBindings.asPtr(), firstReferences[0]);
  namespaceResolution.value.get<BoundNameResolution>().nameSpace = Namespace::Type;
  auto namespaceRejected = BindingVerifier::verify(input, zc::mv(wrongNamespace));
  ZC_EXPECT(requireBinderInvariant(namespaceRejected).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto originCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(originCandidate.is<BindingMetadataCandidate>());
  auto& wrongOrigin = originCandidate.get<BindingMetadataCandidate>();
  auto& originResolution = requireResolution(wrongOrigin.nodeBindings.asPtr(), firstReferences[0]);
  originResolution.value.get<BoundNameResolution>().origin = BindingOrigin::ImportAlias;
  auto originRejected = BindingVerifier::verify(input, zc::mv(wrongOrigin));
  ZC_EXPECT(requireBinderInvariant(originRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto orderCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(orderCandidate.is<BindingMetadataCandidate>());
  auto& wrongOrder = orderCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(wrongOrder.nodeBindings.size() == 2);
  auto displaced = zc::mv(wrongOrder.nodeBindings[0]);
  wrongOrder.nodeBindings[0] = zc::mv(wrongOrder.nodeBindings[1]);
  wrongOrder.nodeBindings[1] = zc::mv(displaced);
  auto orderRejected = BindingVerifier::verify(input, zc::mv(wrongOrder));
  ZC_EXPECT(requireBinderInvariant(orderRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  missing.nodeBindings.removeLast();
  auto missingRejected = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingRejected).kind ==
            BinderInvariantKind::MissingRequiredResolution);
}

ZC_TEST("BindingVerifier.RejectsInvalidFailedResolutionIndex") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { missing; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.nodeBindings.size() == 1);
  ZC_REQUIRE(value.nodeBindings[0].value.is<FailedBindingResolution>());
  value.nodeBindings[0].value.get<FailedBindingResolution>().failureIndex =
      value.sourceFailures.size();
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_EXPECT(requireBinderInvariant(rejected).kind == BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsMalformedShadowFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  let first = 1;\n"
      "  let second = 2;\n"
      "  { let first = first; first; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());

  auto targetCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(targetCandidate.is<BindingMetadataCandidate>());
  auto& wrongTarget = targetCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(wrongTarget.shadowTargets.size() == 1);
  const auto secondDefinition = requireScopeDefinition(wrongTarget.scopes.asPtr(), "second"_zc);
  wrongTarget.shadowTargets[0].target = BindingTarget::definition(secondDefinition);
  auto targetRejected = BindingVerifier::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(targetRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto definitionCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(definitionCandidate.is<BindingMetadataCandidate>());
  auto& wrongDefinition = definitionCandidate.get<BindingMetadataCandidate>();
  wrongDefinition.shadowTargets[0].definition = secondDefinition;
  auto definitionRejected = BindingVerifier::verify(input, zc::mv(wrongDefinition));
  ZC_EXPECT(requireBinderInvariant(definitionRejected).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsForeignBodyBindingIdentities") {
  ParsedSource localSource(
      "module root;\n"
      "fun run() { let value = 1; { let value = value; value; } }\n"_zc);
  ParsedSource foreignSource(
      "module root;\n"
      "fun run() { let value = 1; { let value = value; value; } }\n"_zc);
  FrozenFixture localFixture(localSource, true);
  FrozenFixture foreignFixture(foreignSource, true);
  auto localInputResult = verify(localFixture);
  ZC_REQUIRE(localInputResult.is<VerifiedBindingInput>());
  auto localInput = zc::mv(localInputResult.get<VerifiedBindingInput>());

  auto bindingCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(bindingCandidate.is<BindingMetadataCandidate>());
  auto& foreignBinding = bindingCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(!foreignBinding.nodeBindings.empty());
  ZC_REQUIRE(foreignBinding.nodeBindings[0].value.is<BoundNameResolution>());
  foreignBinding.nodeBindings[0].value.get<BoundNameResolution>().bindingIdentity =
      BindingTarget::definition(foreignFixture.definitionId);
  auto bindingRejected = BindingVerifier::verify(localInput, zc::mv(foreignBinding));
  ZC_EXPECT(requireIdentityInvariant(bindingRejected).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto shadowCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(shadowCandidate.is<BindingMetadataCandidate>());
  auto& foreignShadow = shadowCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(foreignShadow.shadowTargets.size() == 1);
  foreignShadow.shadowTargets[0].target = BindingTarget::definition(foreignFixture.definitionId);
  auto shadowRejected = BindingVerifier::verify(localInput, zc::mv(foreignShadow));
  ZC_EXPECT(requireIdentityInvariant(shadowRejected).kind() ==
            identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("BindingVerifier.RejectsMissingShadowTarget") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { let value = 1; { let value = value; value; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.shadowTargets.size() == 1);
  value.shadowTargets.clear();
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_EXPECT(requireBinderInvariant(rejected).kind ==
            BinderInvariantKind::MissingRequiredResolution);
}

ZC_TEST("BindingActivation.RejectsDuplicateBlockLocals") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun build() {\n"
      "  let value = 1;\n"
      "  let value = 2;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto rejected = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  const auto failures = rejected.get<SourceRejected>().failures();
  ZC_REQUIRE(failures.size() == 1);
  ZC_EXPECT(failures[0].diagnostic == BinderDiagnosticCode::RedeclareVariable);
  ZC_EXPECT((failures[0].emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
  ZC_REQUIRE(failures[0].notes.size() == 1);
  ZC_EXPECT(failures[0].notes[0].diagnostic == BinderDiagnosticCode::PreviousDeclarationHere);
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
