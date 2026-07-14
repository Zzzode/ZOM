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
#include "zomlang/compiler/binder/internal/closure-free-variables.h"
#include "zomlang/compiler/binder/internal/label-facts.h"
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

zc::Vector<ast::NodeId> memberExpressions(const ast::Tree& tree, zc::StringPtr name) {
  zc::Vector<ast::NodeId> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::MemberExpression) { return; }
    const ast::IdentId identifier(syntax.payload.words[ast::kMemberExpressionPropertyWord]);
    if (tree.ident(identifier) == name) { result.add(node); }
  });
  return result;
}

DeferredMemberFact& requireDeferredMember(zc::ArrayPtr<DeferredMemberFact> facts,
                                          ast::NodeId node) {
  for (auto& fact : facts) {
    if (fact.node == node) { return fact; }
  }
  ZC_FAIL_REQUIRE("mutable deferred-member fact is missing");
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
  ZC_FAIL_REQUIRE("invalid control target");
}

LabelFact& requireLabel(zc::ArrayPtr<LabelFact> facts, zc::StringPtr name, size_t occurrence = 0) {
  size_t matching = 0;
  for (auto& fact : facts) {
    if (fact.name.text() == name && matching++ == occurrence) { return fact; }
  }
  ZC_FAIL_REQUIRE("mutable label fact is missing");
}

ScopeId labelTargetScope(const LabelTarget& target) {
  const auto& value = target.value();
  if (value.is<BlockLabelTarget>()) { return value.get<BlockLabelTarget>().scope; }
  return value.get<LoopLabelTarget>().scope;
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

const DefinitionFact& requireDefinitionFact(zc::ArrayPtr<const DefinitionFact> facts,
                                            identity::DefId identity) {
  for (const auto& fact : facts) {
    if (fact.identity == identity) { return fact; }
  }
  ZC_FAIL_REQUIRE("definition fact is missing");
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

identity::DefId requireDefinitionAt(const VerifiedBindingInput& input, ast::NodeId node) {
  auto definition = input.definitions().definitionAt(node);
  ZC_IF_SOME(value, definition) { return value; }
  ZC_FAIL_REQUIRE("definition identity is missing at syntax node");
}

identity::DefId requireNamedFrozenDefinition(const VerifiedBindingInput& input, zc::StringPtr name,
                                             identity::DefinitionKind kind, size_t occurrence = 0) {
  size_t matching = 0;
  for (const auto& entry : input.definitions().definitions()) {
    if (entry.kind != kind || entry.bindingName == zc::none) { continue; }
    ZC_IF_SOME(bindingName, entry.bindingName) {
      if (bindingName.text() == name && matching++ == occurrence) { return entry.definition; }
    }
  }
  ZC_FAIL_REQUIRE("named frozen definition is missing");
}

const ClosureFreeVariableFact& requireClosureFreeVariable(
    zc::ArrayPtr<const ClosureFreeVariableFact> facts, identity::DefId closure) {
  for (const auto& fact : facts) {
    if (fact.closure == closure) { return fact; }
  }
  ZC_FAIL_REQUIRE("closure free-variable fact is missing");
}

ClosureFreeVariableFact& requireClosureFreeVariable(zc::ArrayPtr<ClosureFreeVariableFact> facts,
                                                    identity::DefId closure) {
  for (auto& fact : facts) {
    if (fact.closure == closure) { return fact; }
  }
  ZC_FAIL_REQUIRE("mutable closure free-variable fact is missing");
}

const FreeVariableFact& requireFreeVariable(const ClosureFreeVariableFact& closure,
                                            identity::DefId target) {
  for (const auto& variable : closure.variables) {
    if (variable.target == target) { return variable; }
  }
  ZC_FAIL_REQUIRE("free-variable target is missing");
}

FreeVariableFact& requireFreeVariable(ClosureFreeVariableFact& closure, identity::DefId target) {
  for (auto& variable : closure.variables) {
    if (variable.target == target) { return variable; }
  }
  ZC_FAIL_REQUIRE("mutable free-variable target is missing");
}

FreeVariableFact cloneFreeVariable(const FreeVariableFact& fact) {
  zc::Vector<ast::NodeId> sites;
  for (const auto site : fact.referenceSites) { sites.add(site); }
  return FreeVariableFact{fact.target, zc::mv(sites)};
}

ClosureFreeVariableFact cloneClosureFreeVariable(const ClosureFreeVariableFact& fact) {
  zc::Vector<FreeVariableFact> variables;
  for (const auto& variable : fact.variables) { variables.add(cloneFreeVariable(variable)); }
  return ClosureFreeVariableFact{fact.closure, zc::mv(variables)};
}

const ExplicitClosureCaptureFact& requireExplicitClosureCapture(
    zc::ArrayPtr<const ExplicitClosureCaptureFact> facts, identity::DefId closure) {
  for (const auto& fact : facts) {
    if (fact.closure == closure) { return fact; }
  }
  ZC_FAIL_REQUIRE("explicit closure capture fact is missing");
}

ExplicitClosureCaptureFact& requireExplicitClosureCapture(
    zc::ArrayPtr<ExplicitClosureCaptureFact> facts, identity::DefId closure) {
  for (auto& fact : facts) {
    if (fact.closure == closure) { return fact; }
  }
  ZC_FAIL_REQUIRE("mutable explicit closure capture fact is missing");
}

ExplicitCaptureBindingFact cloneExplicitCapture(const ExplicitCaptureBindingFact& fact) {
  return ExplicitCaptureBindingFact{fact.item, fact.target, fact.source.clone()};
}

ExplicitClosureCaptureFact cloneExplicitClosureCapture(const ExplicitClosureCaptureFact& fact) {
  zc::Vector<ExplicitCaptureBindingFact> captures;
  for (const auto& capture : fact.captures) { captures.add(cloneExplicitCapture(capture)); }
  return ExplicitClosureCaptureFact{fact.closure, fact.captureList, fact.source.clone(),
                                    zc::mv(captures)};
}

zc::Vector<ast::NodeId> identifierExpressionsInSubtree(const ast::Tree& tree, ast::NodeId root,
                                                       zc::StringPtr name) {
  zc::Vector<ast::NodeId> result;
  ast::visitTreePreOrder(tree, root, [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::IdentExpr) { return; }
    const ast::IdentId identifier(syntax.payload.words[ast::kIdentExprNameWord]);
    if (tree.ident(identifier) == name) { result.add(node); }
  });
  return result;
}

uint32_t schemaPreorderOrdinal(const ast::Tree& tree, ast::NodeId wanted) {
  uint32_t ordinal = 0;
  zc::Maybe<uint32_t> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (node == wanted) { result = ordinal; }
    ++ordinal;
  });
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("schema preorder node is missing");
}

void expectCanonicalClosureFreeVariables(const VerifiedBindingInput& input,
                                         zc::ArrayPtr<const ClosureFreeVariableFact> facts) {
  for (size_t index = 1; index < facts.size(); ++index) {
    const auto previous = requireFrozenDefinition(input, facts[index - 1].closure).key.encode();
    const auto current = requireFrozenDefinition(input, facts[index].closure).key.encode();
    ZC_EXPECT(encodedBytesLess(previous.asPtr(), current.asPtr()));
  }
  for (const auto& closure : facts) {
    for (size_t index = 1; index < closure.variables.size(); ++index) {
      const auto previous =
          requireFrozenDefinition(input, closure.variables[index - 1].target).key.encode();
      const auto current =
          requireFrozenDefinition(input, closure.variables[index].target).key.encode();
      ZC_EXPECT(encodedBytesLess(previous.asPtr(), current.asPtr()));
    }
    for (const auto& variable : closure.variables) {
      uint64_t previousStart = 0;
      uint64_t previousEnd = 0;
      uint32_t previousOrdinal = 0;
      bool hasPrevious = false;
      for (const auto site : variable.referenceSites) {
        auto source = input.parsedModule().spanFor(input.tree().node(site).range);
        ZC_REQUIRE(source != zc::none);
        ZC_IF_SOME(value, source) {
          const auto ordinal = schemaPreorderOrdinal(input.tree(), site);
          if (hasPrevious) {
            ZC_EXPECT(previousStart < value.byteStart() ||
                      (previousStart == value.byteStart() && previousEnd < value.byteEnd()) ||
                      (previousStart == value.byteStart() && previousEnd == value.byteEnd() &&
                       previousOrdinal < ordinal));
          }
          previousStart = value.byteStart();
          previousEnd = value.byteEnd();
          previousOrdinal = ordinal;
          hasPrevious = true;
        }
      }
    }
  }
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
  ZC_IF_SOME(dump, encodeBindingAllocationDump(input, output.metadata.scopes(),
                                               output.metadata.labels())) {
    ZC_EXPECT(dump.size() == 3227);
    ZC_IF_SOME(digest, identity::sha256(dump.asPtr())) {
      ZC_EXPECT(zc::encodeHex(digest.bytes()) ==
                "2c5b3604e7bb003b11cff64d1b19af3405ab1940b4379846faba3a05754a9cb6"_zc);
    }
  }
}

ZC_TEST("LabelFacts.ResolvesAllTargetsNestedLabelsAndPreservesControlTransfers") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  block_label: {}\n"
      "  while_label: while (false) {}\n"
      "  for_label: for (;;) {}\n"
      "  for_in_label: for (let item in 0) {}\n"
      "  do_label: do {} while (false);\n"
      "  outer: inner: while (true) { break; continue; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 7);
  ZC_REQUIRE(value.controlTransfers.size() == 2);
  ZC_EXPECT(value.sourceFailures.empty());
  for (size_t index = 0; index < value.labels.size(); ++index) {
    const auto& fact = value.labels[index];
    ZC_REQUIRE(fact.owner.value().is<CallableLabelOwner>());
    ZC_EXPECT(fact.identity.owner() == fact.owner);
    ZC_EXPECT(fact.identity.index() == index);
    ZC_EXPECT(fact.owner.belongsTo(input.semanticContext()));
    ZC_EXPECT(fact.target.belongsTo(input.semanticContext()));
  }
  ZC_EXPECT(
      requireLabel(value.labels.asPtr(), "block_label"_zc).target.value().is<BlockLabelTarget>());
  const zc::StringPtr loopLabels[] = {"while_label"_zc, "for_label"_zc, "for_in_label"_zc,
                                      "do_label"_zc,    "outer"_zc,     "inner"_zc};
  for (const auto name : loopLabels) {
    ZC_EXPECT(requireLabel(value.labels.asPtr(), name).target.value().is<LoopLabelTarget>());
  }
  const auto& outer = requireLabel(value.labels.asPtr(), "outer"_zc);
  const auto& inner = requireLabel(value.labels.asPtr(), "inner"_zc);
  ZC_EXPECT(input.tree().node(outer.statement).kind == ast::SyntaxKind::LabeledStatement);
  ZC_EXPECT(input.tree().node(inner.statement).kind == ast::SyntaxKind::WhileStmt);
  ZC_EXPECT(labelTargetScope(outer.target) == labelTargetScope(inner.target));
  const auto labeledNodes = nodesOfKind(input.tree(), ast::SyntaxKind::LabeledStatement);
  ZC_REQUIRE(labeledNodes.size() == 7);
  zc::Maybe<ScopeId> outerLabelScope;
  zc::Maybe<ScopeId> innerLabelScope;
  for (const auto& fact : value.nodeScopes) {
    if (fact.node == labeledNodes[5]) { outerLabelScope = fact.scope; }
    if (fact.node == labeledNodes[6]) { innerLabelScope = fact.scope; }
  }
  ZC_REQUIRE(outerLabelScope != zc::none);
  ZC_REQUIRE(innerLabelScope != zc::none);
  ZC_IF_SOME(outerScope, outerLabelScope) {
    ZC_IF_SOME(innerScope, innerLabelScope) { ZC_EXPECT(outerScope == innerScope); }
  }
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
  ZC_EXPECT(verification.get<VerifiedBindingOutput>().metadata.labels().size() == 7);
}

ZC_TEST("LabelFacts.AllocatesModuleAndCallableOwnerIndicesIndependently") {
  ParsedSource sourceFixture(
      "module root;\n"
      "module_first: {}\n"
      "fun alpha() { shared: {} alpha_second: {} }\n"
      "module_second: while (false) {}\n"
      "fun beta() { shared: {} }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 5);
  ZC_EXPECT(value.sourceFailures.empty());

  const auto& moduleFirst = requireLabel(value.labels.asPtr(), "module_first"_zc);
  const auto& moduleSecond = requireLabel(value.labels.asPtr(), "module_second"_zc);
  ZC_REQUIRE(moduleFirst.owner.value().is<ModuleLabelOwner>());
  ZC_REQUIRE(moduleSecond.owner.value().is<ModuleLabelOwner>());
  ZC_EXPECT(moduleFirst.identity.index() == 0);
  ZC_EXPECT(moduleSecond.identity.index() == 1);
  const auto alpha = requireScopeDefinition(value.scopes.asPtr(), "alpha"_zc);
  const auto beta = requireScopeDefinition(value.scopes.asPtr(), "beta"_zc);
  const auto& alphaSecond = requireLabel(value.labels.asPtr(), "alpha_second"_zc);
  ZC_EXPECT(alphaSecond.owner.value().get<CallableLabelOwner>().callable == alpha);
  ZC_EXPECT(alphaSecond.identity.index() == 1);
  bool foundAlphaShared = false;
  bool foundBetaShared = false;
  for (const auto& fact : value.labels) {
    if (fact.name.text() != "shared"_zc) { continue; }
    ZC_REQUIRE(fact.owner.value().is<CallableLabelOwner>());
    const auto callable = fact.owner.value().get<CallableLabelOwner>().callable;
    if (callable == alpha) { foundAlphaShared = true; }
    if (callable == beta) { foundBetaShared = true; }
    ZC_EXPECT(fact.identity.index() == 0);
  }
  ZC_EXPECT(foundAlphaShared);
  ZC_EXPECT(foundBetaShared);
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
}

ZC_TEST("LabelFacts.ResetsClosureOwnerIndicesIndependently") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  function_first: {}\n"
      "  let closure = fun() { closure_first: {} closure_second: {} };\n"
      "  function_second: {}\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 4);
  ZC_EXPECT(value.sourceFailures.empty());

  const auto function = requireScopeDefinition(value.scopes.asPtr(), "run"_zc);
  const auto& functionFirst = requireLabel(value.labels.asPtr(), "function_first"_zc);
  const auto& functionSecond = requireLabel(value.labels.asPtr(), "function_second"_zc);
  ZC_REQUIRE(functionFirst.owner.value().is<CallableLabelOwner>());
  ZC_REQUIRE(functionSecond.owner.value().is<CallableLabelOwner>());
  ZC_EXPECT(functionFirst.owner.value().get<CallableLabelOwner>().callable == function);
  ZC_EXPECT(functionSecond.owner.value().get<CallableLabelOwner>().callable == function);
  ZC_EXPECT(functionFirst.identity.index() == 0);
  ZC_EXPECT(functionSecond.identity.index() == 1);

  const auto& closureFirst = requireLabel(value.labels.asPtr(), "closure_first"_zc);
  const auto& closureSecond = requireLabel(value.labels.asPtr(), "closure_second"_zc);
  ZC_REQUIRE(closureFirst.owner.value().is<CallableLabelOwner>());
  ZC_REQUIRE(closureSecond.owner.value().is<CallableLabelOwner>());
  const auto closure = closureFirst.owner.value().get<CallableLabelOwner>().callable;
  ZC_EXPECT(closure != function);
  ZC_EXPECT(closureSecond.owner.value().get<CallableLabelOwner>().callable == closure);
  ZC_EXPECT(closureFirst.identity.index() == 0);
  ZC_EXPECT(closureSecond.identity.index() == 1);

  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
}

ZC_TEST("LabelFacts.SortsCallableOwnersByCanonicalDefinitionKey") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun zulu() { zulu_label: {} }\n"
      "fun alfa() { alfa_label: {} }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 2);
  ZC_EXPECT(value.sourceFailures.empty());

  const auto zulu = requireScopeDefinition(value.scopes.asPtr(), "zulu"_zc);
  const auto alfa = requireScopeDefinition(value.scopes.asPtr(), "alfa"_zc);
  auto zuluKey = input.definitions().definitionKey(zulu);
  auto alfaKey = input.definitions().definitionKey(alfa);
  ZC_REQUIRE(zuluKey != zc::none);
  ZC_REQUIRE(alfaKey != zc::none);
  const auto& zuluLabel = requireLabel(value.labels.asPtr(), "zulu_label"_zc);
  const auto& alfaLabel = requireLabel(value.labels.asPtr(), "alfa_label"_zc);
  ZC_EXPECT(zuluLabel.source.byteStart() < alfaLabel.source.byteStart());
  ZC_IF_SOME(zuluKeyValue, zuluKey) {
    ZC_IF_SOME(alfaKeyValue, alfaKey) {
      const auto zuluBytes = zuluKeyValue.encode();
      const auto alfaBytes = alfaKeyValue.encode();
      ZC_EXPECT(encodedBytesLess(alfaBytes.asPtr(), zuluBytes.asPtr()));
    }
  }
  ZC_EXPECT(value.labels[0].name.text() == "alfa_label"_zc);
  ZC_EXPECT(value.labels[1].name.text() == "zulu_label"_zc);
  ZC_EXPECT(value.labels[0].identity.index() == 0);
  ZC_EXPECT(value.labels[1].identity.index() == 0);

  auto allocation = encodeBindingAllocationDump(input, value.scopes.asPtr(), value.labels.asPtr());
  ZC_REQUIRE(allocation != zc::none);
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
}

ZC_TEST("LabelFacts.ReportsSiblingNestedAndNfcEquivalentDuplicates") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  caf\\u00e9: {} cafe\\u0301: {}\n"
      "  duplicate: {} duplicate: while (false) {}\n"
      "  nested: nested: while (false) {}\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 6);
  ZC_REQUIRE(value.sourceFailures.size() == 3);
  ZC_EXPECT(requireLabel(value.labels.asPtr(), "caf\xc3\xa9"_zc, 0).identity.index() == 0);
  ZC_EXPECT(requireLabel(value.labels.asPtr(), "caf\xc3\xa9"_zc, 1).identity.index() == 1);
  const auto labeledNodes = nodesOfKind(input.tree(), ast::SyntaxKind::LabeledStatement);
  ZC_REQUIRE(labeledNodes.size() == 6);
  for (size_t index = 0; index < value.sourceFailures.size(); ++index) {
    const auto& failureFact = value.sourceFailures[index];
    ZC_EXPECT(failureFact.diagnostic == BinderDiagnosticCode::DuplicateIdentifier);
    ZC_EXPECT(static_cast<uint8_t>(failureFact.emitterOrdinal >> 56) ==
              static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure));
    ZC_EXPECT(static_cast<uint16_t>(failureFact.emitterOrdinal) == 0);
    ZC_REQUIRE(failureFact.notes.size() == 1);
    ZC_EXPECT(failureFact.notes[0].diagnostic == BinderDiagnosticCode::PreviousDeclarationHere);
  }
  ZC_EXPECT(value.sourceFailures[0].primary.byteEnd() -
                value.sourceFailures[0].primary.byteStart() ==
            10);
  ZC_EXPECT(value.sourceFailures[0].notes[0].source.byteEnd() -
                value.sourceFailures[0].notes[0].source.byteStart() ==
            9);
  for (const auto& binding : value.nodeBindings) {
    for (const auto node : labeledNodes) { ZC_EXPECT(binding.node != node); }
  }
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<SourceRejected>());
  ZC_EXPECT(verification.get<SourceRejected>().failures().size() == 3);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 3);
}

ZC_TEST("LabelFacts.RejectsLabelIndexOverflow") {
  ZC_IF_SOME(maximum, checkedLabelIndex(UINT32_MAX)) { ZC_EXPECT(maximum == UINT32_MAX); }
  ZC_EXPECT(checkedLabelIndex(static_cast<uint64_t>(UINT32_MAX) + 1) == zc::none);
}

ZC_TEST("BindingAllocationDump.EncodesSchemaBackedLabelRecords") {
  ParsedSource sourceFixture(
      "module root;\n"
      "first: {}\n"
      "fun run() { second: while (false) {} }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 2);
  auto complete = encodeBindingAllocationDump(input, value.scopes.asPtr(), value.labels.asPtr());
  auto scopesOnly =
      encodeBindingAllocationDump(input, value.scopes.asPtr(), zc::ArrayPtr<const LabelFact>());
  ZC_REQUIRE(complete != zc::none);
  ZC_REQUIRE(scopesOnly != zc::none);
  ZC_IF_SOME(completeValue, complete) {
    ZC_IF_SOME(scopeValue, scopesOnly) {
      ZC_EXPECT(completeValue.size() > scopeValue.size());
      ZC_EXPECT(completeValue.asPtr() != scopeValue.asPtr());
    }
    ZC_IF_SOME(digest, identity::sha256(completeValue.asPtr())) {
      ZC_EXPECT(zc::encodeHex(digest.bytes()) ==
                "c6e9e1a3febdc564be5fa5613435ad515943f4b16a47b186d0bf19f81c91e659"_zc);
    }
  }
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
}

ZC_TEST("BindingAllocationDump.RejectsNonCanonicalLabelsAndInvalidTargetScopes") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { first: {} second: {} }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());

  auto reorderedCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(reorderedCandidate.is<BindingMetadataCandidate>());
  auto& reordered = reorderedCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(reordered.labels.size() == 2);
  auto first = zc::mv(reordered.labels[0]);
  reordered.labels[0] = zc::mv(reordered.labels[1]);
  reordered.labels[1] = zc::mv(first);
  ZC_EXPECT(encodeBindingAllocationDump(input, reordered.scopes.asPtr(),
                                        reordered.labels.asPtr()) == zc::none);

  auto duplicateCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  auto identityDonorCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(duplicateCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(identityDonorCandidate.is<BindingMetadataCandidate>());
  auto& duplicate = duplicateCandidate.get<BindingMetadataCandidate>();
  auto& identityDonor = identityDonorCandidate.get<BindingMetadataCandidate>();
  duplicate.labels[1].identity = zc::mv(identityDonor.labels[0].identity);
  ZC_EXPECT(encodeBindingAllocationDump(input, duplicate.scopes.asPtr(),
                                        duplicate.labels.asPtr()) == zc::none);

  auto wrongKindCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(wrongKindCandidate.is<BindingMetadataCandidate>());
  auto& wrongKind = wrongKindCandidate.get<BindingMetadataCandidate>();
  const auto wrongKindScope = labelTargetScope(wrongKind.labels[0].target);
  ZC_REQUIRE(wrongKindScope.index() < wrongKind.scopes.size());
  wrongKind.scopes[wrongKindScope.index()].kind = ScopeKind::Loop;
  ZC_EXPECT(encodeBindingAllocationDump(input, wrongKind.scopes.asPtr(),
                                        wrongKind.labels.asPtr()) == zc::none);

  auto missingScopeCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingScopeCandidate.is<BindingMetadataCandidate>());
  auto& missingScope = missingScopeCandidate.get<BindingMetadataCandidate>();
  const auto missingTarget = labelTargetScope(missingScope.labels[1].target);
  ZC_REQUIRE(missingTarget.index() + 1 == missingScope.scopes.size());
  missingScope.scopes.removeLast();
  ZC_EXPECT(encodeBindingAllocationDump(input, missingScope.scopes.asPtr(),
                                        missingScope.labels.asPtr()) == zc::none);
}

ZC_TEST("BindingVerifier.RejectsMissingAdditionalReorderedAndMutatedLabels") {
  ParsedSource sourceFixture(
      "module root;\n"
      "module_label: {}\n"
      "fun run() { first: {} second: while (false) {} }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missing.labels.size() == 3);
  missing.labels.removeLast();
  auto missingResult = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  auto donorCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(donorCandidate.is<BindingMetadataCandidate>());
  auto& additional = additionalCandidate.get<BindingMetadataCandidate>();
  auto& donor = donorCandidate.get<BindingMetadataCandidate>();
  additional.labels.add(zc::mv(donor.labels[0]));
  auto additionalResult = BindingVerifier::verify(input, zc::mv(additional));
  ZC_EXPECT(requireBinderInvariant(additionalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(reorderedCandidate.is<BindingMetadataCandidate>());
  auto& reordered = reorderedCandidate.get<BindingMetadataCandidate>();
  auto firstFact = zc::mv(reordered.labels[0]);
  reordered.labels[0] = zc::mv(reordered.labels[1]);
  reordered.labels[1] = zc::mv(firstFact);
  auto reorderedResult = BindingVerifier::verify(input, zc::mv(reordered));
  ZC_EXPECT(requireBinderInvariant(reorderedResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto identityCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(identityCandidate.is<BindingMetadataCandidate>());
  auto& wrongIdentity = identityCandidate.get<BindingMetadataCandidate>();
  auto firstIdentity = zc::mv(wrongIdentity.labels[1].identity);
  wrongIdentity.labels[1].identity = zc::mv(wrongIdentity.labels[2].identity);
  wrongIdentity.labels[2].identity = zc::mv(firstIdentity);
  auto identityResult = BindingVerifier::verify(input, zc::mv(wrongIdentity));
  ZC_EXPECT(requireBinderInvariant(identityResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto ownerCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(ownerCandidate.is<BindingMetadataCandidate>());
  auto& wrongOwner = ownerCandidate.get<BindingMetadataCandidate>();
  auto moduleOwner = zc::mv(wrongOwner.labels[0].owner);
  wrongOwner.labels[0].owner = zc::mv(wrongOwner.labels[1].owner);
  wrongOwner.labels[1].owner = zc::mv(moduleOwner);
  auto ownerResult = BindingVerifier::verify(input, zc::mv(wrongOwner));
  ZC_EXPECT(requireBinderInvariant(ownerResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto statementCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(statementCandidate.is<BindingMetadataCandidate>());
  auto& wrongStatement = statementCandidate.get<BindingMetadataCandidate>();
  wrongStatement.labels[1].statement = wrongStatement.labels[2].statement;
  auto statementResult = BindingVerifier::verify(input, zc::mv(wrongStatement));
  ZC_EXPECT(requireBinderInvariant(statementResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto targetCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(targetCandidate.is<BindingMetadataCandidate>());
  auto& wrongTarget = targetCandidate.get<BindingMetadataCandidate>();
  auto firstTarget = zc::mv(wrongTarget.labels[1].target);
  wrongTarget.labels[1].target = zc::mv(wrongTarget.labels[2].target);
  wrongTarget.labels[2].target = zc::mv(firstTarget);
  auto targetResult = BindingVerifier::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(targetResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto sourceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(sourceCandidate.is<BindingMetadataCandidate>());
  auto& wrongSource = sourceCandidate.get<BindingMetadataCandidate>();
  wrongSource.labels[1].source = wrongSource.labels[2].source.clone();
  auto sourceResult = BindingVerifier::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(sourceResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto nameCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(nameCandidate.is<BindingMetadataCandidate>());
  auto& wrongName = nameCandidate.get<BindingMetadataCandidate>();
  auto replacementName = identity::SemanticIdentifier::fromCanonical("changed"_zc);
  ZC_REQUIRE(replacementName != zc::none);
  ZC_IF_SOME(name, replacementName) { wrongName.labels[1].name = zc::mv(name); }
  auto nameResult = BindingVerifier::verify(input, zc::mv(wrongName));
  ZC_EXPECT(requireBinderInvariant(nameResult).kind == BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsForeignLabelOwnersAndTargets") {
  ParsedSource localSource("module root;\nfun run() { local: while (false) {} }\n"_zc);
  ParsedSource foreignSource("module root;\nfun run() { local: while (false) {} }\n"_zc);
  FrozenFixture localFixture(localSource, true);
  FrozenFixture foreignFixture(foreignSource, true);
  auto localInputResult = verify(localFixture);
  auto foreignInputResult = verify(foreignFixture);
  ZC_REQUIRE(localInputResult.is<VerifiedBindingInput>());
  ZC_REQUIRE(foreignInputResult.is<VerifiedBindingInput>());
  auto localInput = zc::mv(localInputResult.get<VerifiedBindingInput>());
  auto foreignInput = zc::mv(foreignInputResult.get<VerifiedBindingInput>());

  auto foreignOwnerCandidate = BindingBuilder::build(foreignInput, *foreignSource.diagnostics);
  auto localOwnerCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(foreignOwnerCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(localOwnerCandidate.is<BindingMetadataCandidate>());
  localOwnerCandidate.get<BindingMetadataCandidate>().labels[0].owner =
      zc::mv(foreignOwnerCandidate.get<BindingMetadataCandidate>().labels[0].owner);
  auto ownerResult = BindingVerifier::verify(
      localInput, zc::mv(localOwnerCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireIdentityInvariant(ownerResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto foreignIdentityCandidate = BindingBuilder::build(foreignInput, *foreignSource.diagnostics);
  auto localIdentityCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(foreignIdentityCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(localIdentityCandidate.is<BindingMetadataCandidate>());
  localIdentityCandidate.get<BindingMetadataCandidate>().labels[0].identity =
      zc::mv(foreignIdentityCandidate.get<BindingMetadataCandidate>().labels[0].identity);
  auto identityResult = BindingVerifier::verify(
      localInput, zc::mv(localIdentityCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireIdentityInvariant(identityResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto foreignTargetCandidate = BindingBuilder::build(foreignInput, *foreignSource.diagnostics);
  auto localTargetCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(foreignTargetCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(localTargetCandidate.is<BindingMetadataCandidate>());
  localTargetCandidate.get<BindingMetadataCandidate>().labels[0].target =
      zc::mv(foreignTargetCandidate.get<BindingMetadataCandidate>().labels[0].target);
  auto targetResult = BindingVerifier::verify(
      localInput, zc::mv(localTargetCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireIdentityInvariant(targetResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("BindingVerifier.RejectsMalformedLabelDuplicateFailuresAndDeclarationBindings") {
  ParsedSource sourceFixture(
      "module root;\nfun run() { duplicate: {} duplicate: while (false) {} }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto labeledNodes = nodesOfKind(input.tree(), ast::SyntaxKind::LabeledStatement);
  ZC_REQUIRE(labeledNodes.size() == 2);

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missing.sourceFailures.size() == 1);
  missing.sourceFailures.removeLast();
  auto missingResult = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto diagnosticCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(diagnosticCandidate.is<BindingMetadataCandidate>());
  auto& wrongDiagnostic = diagnosticCandidate.get<BindingMetadataCandidate>();
  wrongDiagnostic.sourceFailures[0].diagnostic = BinderDiagnosticCode::UndefinedIdentifier;
  auto diagnosticResult = BindingVerifier::verify(input, zc::mv(wrongDiagnostic));
  ZC_EXPECT(requireBinderInvariant(diagnosticResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto noteCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(noteCandidate.is<BindingMetadataCandidate>());
  auto& missingNote = noteCandidate.get<BindingMetadataCandidate>();
  missingNote.sourceFailures[0].notes.removeLast();
  auto noteResult = BindingVerifier::verify(input, zc::mv(missingNote));
  ZC_EXPECT(requireBinderInvariant(noteResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto primaryCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(primaryCandidate.is<BindingMetadataCandidate>());
  auto& wrongPrimary = primaryCandidate.get<BindingMetadataCandidate>();
  wrongPrimary.sourceFailures[0].primary = wrongPrimary.sourceFailures[0].notes[0].source.clone();
  auto primaryResult = BindingVerifier::verify(input, zc::mv(wrongPrimary));
  ZC_EXPECT(requireBinderInvariant(primaryResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto noteSourceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(noteSourceCandidate.is<BindingMetadataCandidate>());
  auto& wrongNoteSource = noteSourceCandidate.get<BindingMetadataCandidate>();
  wrongNoteSource.sourceFailures[0].notes[0].source =
      wrongNoteSource.sourceFailures[0].primary.clone();
  auto noteSourceResult = BindingVerifier::verify(input, zc::mv(wrongNoteSource));
  ZC_EXPECT(requireBinderInvariant(noteSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto ordinalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(ordinalCandidate.is<BindingMetadataCandidate>());
  auto& wrongOrdinal = ordinalCandidate.get<BindingMetadataCandidate>();
  ++wrongOrdinal.sourceFailures[0].emitterOrdinal;
  auto ordinalResult = BindingVerifier::verify(input, zc::mv(wrongOrdinal));
  ZC_EXPECT(requireBinderInvariant(ordinalResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto extraCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(extraCandidate.is<BindingMetadataCandidate>());
  auto& extraFailure = extraCandidate.get<BindingMetadataCandidate>();
  const auto& original = extraFailure.sourceFailures[0];
  zc::Vector<BindingDiagnosticNoteRef> extraNotes;
  extraNotes.add(
      BindingDiagnosticNoteRef{original.notes[0].diagnostic, original.notes[0].source.clone()});
  extraFailure.sourceFailures.add(BindingFailureRef{original.diagnostic, original.primary.clone(),
                                                    original.emitterOrdinal, zc::mv(extraNotes)});
  auto extraResult = BindingVerifier::verify(input, zc::mv(extraFailure));
  ZC_EXPECT(requireBinderInvariant(extraResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto bindingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(bindingCandidate.is<BindingMetadataCandidate>());
  auto& declarationBinding = bindingCandidate.get<BindingMetadataCandidate>();
  declarationBinding.nodeBindings.add(
      BindingResolution{labeledNodes[1], BindingResolutionValue(FailedBindingResolution{0})});
  auto bindingResult = BindingVerifier::verify(input, zc::mv(declarationBinding));
  ZC_EXPECT(requireBinderInvariant(bindingResult).kind == BinderInvariantKind::InvalidBindingFact);
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

ZC_TEST("ControlTransfer.ResolvesExplicitBlockLoopAndNestedLabels") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  block_label: { break block_label; }\n"
      "  while_label: while (true) { break while_label; continue while_label; }\n"
      "  for_label: for (;;) { break for_label; continue for_label; }\n"
      "  for_in_label: for (let item in 0) { break for_in_label; continue for_in_label; }\n"
      "  do_label: do { break do_label; continue do_label; } while (false);\n"
      "  outer: inner: while (true) { break outer; continue inner; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 7);
  ZC_REQUIRE(value.controlTransfers.size() == 11);
  ZC_REQUIRE(value.nodeBindings.size() == 11);
  for (const auto& fact : value.controlTransfers) {
    ZC_REQUIRE(fact.target.is<ExplicitLabelControlTarget>());
    const auto& syntax = input.tree().node(fact.node);
    const bool isBreak = syntax.kind == ast::SyntaxKind::BreakStmt;
    const ast::IdentId label(syntax.payload.words[isBreak ? ast::kBreakStmtLabelWord
                                                          : ast::kContinueStatementLabelWord]);
    const auto& expected = requireLabel(value.labels.asPtr(), input.tree().ident(label));
    ZC_EXPECT(fact.target.get<ExplicitLabelControlTarget>().label == expected.identity);
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), fact.node);
    ZC_REQUIRE(resolution.value.is<BoundLabelResolution>());
    const auto& bound = resolution.value.get<BoundLabelResolution>();
    ZC_EXPECT(bound.label == expected.identity);
    ZC_EXPECT(bound.target == expected.target);
    if (!isBreak) { ZC_EXPECT(bound.target.value().is<LoopLabelTarget>()); }
    auto statementSource = input.parsedModule().spanFor(syntax.range);
    ZC_REQUIRE(statementSource != zc::none);
    ZC_IF_SOME(source, statementSource) { ZC_EXPECT(sameSpan(fact.source, source)); }
  }
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
}

ZC_TEST("ControlTransfer.ResolvesCanonicalEscapedLabels") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { caf\\u00e9: while (true) { break cafe\\u0301; continue caf\\u00e9; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 1);
  ZC_REQUIRE(value.controlTransfers.size() == 2);
  ZC_REQUIRE(value.nodeBindings.size() == 2);
  ZC_EXPECT(value.sourceFailures.empty());
  const auto& label = requireLabel(value.labels.asPtr(), "caf\xc3\xa9"_zc);
  for (const auto& fact : value.controlTransfers) {
    ZC_REQUIRE(fact.target.is<ExplicitLabelControlTarget>());
    ZC_EXPECT(fact.target.get<ExplicitLabelControlTarget>().label == label.identity);
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), fact.node);
    ZC_REQUIRE(resolution.value.is<BoundLabelResolution>());
    ZC_EXPECT(resolution.value.get<BoundLabelResolution>().label == label.identity);
  }
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
}

ZC_TEST("ControlTransfer.ResolvesActiveDuplicateLabelsAlongsideDiagnostics") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  sibling: {} sibling: while (true) { break sibling; }\n"
      "  nested: nested: while (true) { break nested; continue nested; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 4);
  ZC_REQUIRE(value.controlTransfers.size() == 3);
  ZC_REQUIRE(value.sourceFailures.size() == 2);
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(breaks.size() == 2);
  ZC_REQUIRE(continues.size() == 1);
  const auto& activeSibling = requireLabel(value.labels.asPtr(), "sibling"_zc, 1);
  const auto& activeNested = requireLabel(value.labels.asPtr(), "nested"_zc, 1);
  const auto expectTarget = [&](ast::NodeId node, const LabelFact& expected) {
    const auto& fact = requireControlTransfer(value.controlTransfers.asPtr(), node);
    ZC_REQUIRE(fact.target.is<ExplicitLabelControlTarget>());
    ZC_EXPECT(fact.target.get<ExplicitLabelControlTarget>().label == expected.identity);
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), node);
    ZC_REQUIRE(resolution.value.is<BoundLabelResolution>());
    ZC_EXPECT(resolution.value.get<BoundLabelResolution>().label == expected.identity);
  };
  expectTarget(breaks[0], activeSibling);
  expectTarget(breaks[1], activeNested);
  expectTarget(continues[0], activeNested);
  for (const auto& failure : value.sourceFailures) {
    ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::DuplicateIdentifier);
  }
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<SourceRejected>());
  ZC_EXPECT(verification.get<SourceRejected>().failures().size() == 2);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 2);
}

ZC_TEST("ControlTransfer.ResolvesModuleOwnedLabels") {
  ParsedSource sourceFixture("module root;\nmodule_label: { break module_label; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 1);
  ZC_REQUIRE(value.controlTransfers.size() == 1);
  ZC_REQUIRE(value.nodeBindings.size() == 1);
  const auto& label = value.labels[0];
  ZC_REQUIRE(label.owner.value().is<ModuleLabelOwner>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  ZC_REQUIRE(breaks.size() == 1);
  const auto& fact = requireControlTransfer(value.controlTransfers.asPtr(), breaks[0]);
  ZC_REQUIRE(fact.target.is<ExplicitLabelControlTarget>());
  ZC_EXPECT(fact.target.get<ExplicitLabelControlTarget>().label == label.identity);
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), breaks[0]);
  ZC_REQUIRE(resolution.value.is<BoundLabelResolution>());
  ZC_EXPECT(resolution.value.get<BoundLabelResolution>().label == label.identity);
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
}

ZC_TEST("ControlTransfer.RejectsInactiveAndCrossClosureLabelsWithoutFallback") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  while (true) { break miss\\u0069ng; }\n"
      "  break forward; forward: {}\n"
      "  first: {} second: { break first; }\n"
      "  outer: while (true) {\n"
      "    let closure = fun() { while (true) { continue outer; } };\n"
      "  }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 4);
  ZC_EXPECT(value.controlTransfers.empty());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(breaks.size() == 3);
  ZC_REQUIRE(continues.size() == 1);
  for (const auto node : breaks) {
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), node);
    ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
    const size_t failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
    ZC_REQUIRE(failureIndex < value.sourceFailures.size());
    const auto& failure = value.sourceFailures[failureIndex];
    ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
    ZC_EXPECT(static_cast<uint8_t>(failure.emitterOrdinal >> 56) ==
              static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure));
    auto expected = input.parsedModule().retainedTokenSpan(node, 1, ast::SyntaxKind::Identifier);
    ZC_REQUIRE(expected != zc::none);
    ZC_IF_SOME(source, expected) { ZC_EXPECT(sameSpan(failure.primary, source)); }
  }
  const auto& continueResolution = requireResolution(value.nodeBindings.asPtr(), continues[0]);
  ZC_REQUIRE(continueResolution.value.is<FailedBindingResolution>());
  const size_t continueFailure =
      continueResolution.value.get<FailedBindingResolution>().failureIndex;
  ZC_REQUIRE(continueFailure < value.sourceFailures.size());
  ZC_EXPECT(value.sourceFailures[continueFailure].diagnostic ==
            BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT(value.sourceFailures[0].primary.byteEnd() -
                value.sourceFailures[0].primary.byteStart() ==
            12);
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(rejected.get<SourceRejected>().failures().size() == 4);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 4);
}

ZC_TEST("ControlTransfer.RejectsContinueToBlockLabelWithoutLoopFallback") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() { block_label: { while (true) { continue block_label; } } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.labels.size() == 1);
  ZC_EXPECT(value.controlTransfers.empty());
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(continues.size() == 1);
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), continues[0]);
  ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
  const size_t failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
  ZC_REQUIRE(failureIndex < value.sourceFailures.size());
  const auto& failure = value.sourceFailures[failureIndex];
  ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::ContinueTargetNotLoop);
  ZC_EXPECT(static_cast<uint8_t>(failure.emitterOrdinal >> 56) ==
            static_cast<uint8_t>(BinderEmitterSite::BodyBinding));
  auto expected =
      input.parsedModule().retainedTokenSpan(continues[0], 1, ast::SyntaxKind::Identifier);
  ZC_REQUIRE(expected != zc::none);
  ZC_IF_SOME(source, expected) { ZC_EXPECT(sameSpan(failure.primary, source)); }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_REQUIRE(rejected.get<SourceRejected>().failures().size() == 1);
  ZC_EXPECT(rejected.get<SourceRejected>().failures()[0].diagnostic ==
            BinderDiagnosticCode::ContinueTargetNotLoop);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("ControlTransfer.OrdersMixedLabelAndBodyFailures") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  duplicate: {}\n"
      "  while (true) { break absent; }\n"
      "  missing_value;\n"
      "  duplicate: {}\n"
      "  block_label: { while (true) { continue block_label; } }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 4);
  ZC_EXPECT(value.controlTransfers.empty());
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT(value.sourceFailures[1].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT(value.sourceFailures[2].diagnostic == BinderDiagnosticCode::DuplicateIdentifier);
  ZC_EXPECT(value.sourceFailures[3].diagnostic == BinderDiagnosticCode::ContinueTargetNotLoop);
  ZC_EXPECT(static_cast<uint8_t>(value.sourceFailures[0].emitterOrdinal >> 56) ==
            static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure));
  ZC_EXPECT(static_cast<uint8_t>(value.sourceFailures[1].emitterOrdinal >> 56) ==
            static_cast<uint8_t>(BinderEmitterSite::BodyBinding));
  ZC_EXPECT(static_cast<uint8_t>(value.sourceFailures[2].emitterOrdinal >> 56) ==
            static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure));
  ZC_EXPECT(static_cast<uint8_t>(value.sourceFailures[3].emitterOrdinal >> 56) ==
            static_cast<uint8_t>(BinderEmitterSite::BodyBinding));
  auto verification = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verification.is<SourceRejected>());
  ZC_EXPECT(verification.get<SourceRejected>().failures().size() == 4);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 4);
}

ZC_TEST("BindingVerifier.RejectsMalformedExplicitLabelSuccessPairs") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  first: while (true) { break first; }\n"
      "  second: while (true) { continue second; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  const auto continues = nodesOfKind(input.tree(), ast::SyntaxKind::ContinueStatement);
  ZC_REQUIRE(breaks.size() == 1);
  ZC_REQUIRE(continues.size() == 1);

  auto missingResolutionCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingResolutionCandidate.is<BindingMetadataCandidate>());
  auto& missingResolution = missingResolutionCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missingResolution.nodeBindings.size() == 2);
  missingResolution.nodeBindings.removeLast();
  auto missingResolutionResult = BindingVerifier::verify(input, zc::mv(missingResolution));
  ZC_EXPECT(requireBinderInvariant(missingResolutionResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto missingPairCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingPairCandidate.is<BindingMetadataCandidate>());
  auto& missingPair = missingPairCandidate.get<BindingMetadataCandidate>();
  missingPair.nodeBindings.removeLast();
  missingPair.controlTransfers.removeLast();
  auto missingPairResult = BindingVerifier::verify(input, zc::mv(missingPair));
  ZC_EXPECT(requireBinderInvariant(missingPairResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto boundLabelCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(boundLabelCandidate.is<BindingMetadataCandidate>());
  auto& wrongBoundLabel = boundLabelCandidate.get<BindingMetadataCandidate>();
  auto& breakBound = requireResolution(wrongBoundLabel.nodeBindings.asPtr(), breaks[0])
                         .value.get<BoundLabelResolution>();
  auto& continueBound = requireResolution(wrongBoundLabel.nodeBindings.asPtr(), continues[0])
                            .value.get<BoundLabelResolution>();
  auto savedBoundLabel = zc::mv(breakBound.label);
  breakBound.label = zc::mv(continueBound.label);
  continueBound.label = zc::mv(savedBoundLabel);
  auto boundLabelResult = BindingVerifier::verify(input, zc::mv(wrongBoundLabel));
  ZC_EXPECT(requireBinderInvariant(boundLabelResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto boundTargetCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(boundTargetCandidate.is<BindingMetadataCandidate>());
  auto& wrongBoundTarget = boundTargetCandidate.get<BindingMetadataCandidate>();
  auto& firstTarget = requireResolution(wrongBoundTarget.nodeBindings.asPtr(), breaks[0])
                          .value.get<BoundLabelResolution>();
  auto& secondTarget = requireResolution(wrongBoundTarget.nodeBindings.asPtr(), continues[0])
                           .value.get<BoundLabelResolution>();
  auto savedBoundTarget = zc::mv(firstTarget.target);
  firstTarget.target = zc::mv(secondTarget.target);
  secondTarget.target = zc::mv(savedBoundTarget);
  auto boundTargetResult = BindingVerifier::verify(input, zc::mv(wrongBoundTarget));
  ZC_EXPECT(requireBinderInvariant(boundTargetResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto factLabelCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(factLabelCandidate.is<BindingMetadataCandidate>());
  auto& wrongFactLabel = factLabelCandidate.get<BindingMetadataCandidate>();
  auto& firstFactLabel = requireControlTransfer(wrongFactLabel.controlTransfers.asPtr(), breaks[0])
                             .target.get<ExplicitLabelControlTarget>();
  auto& secondFactLabel =
      requireControlTransfer(wrongFactLabel.controlTransfers.asPtr(), continues[0])
          .target.get<ExplicitLabelControlTarget>();
  auto savedFactLabel = zc::mv(firstFactLabel.label);
  firstFactLabel.label = zc::mv(secondFactLabel.label);
  secondFactLabel.label = zc::mv(savedFactLabel);
  auto factLabelResult = BindingVerifier::verify(input, zc::mv(wrongFactLabel));
  ZC_EXPECT(requireBinderInvariant(factLabelResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto algebraCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(algebraCandidate.is<BindingMetadataCandidate>());
  auto& wrongAlgebra = algebraCandidate.get<BindingMetadataCandidate>();
  const auto loopScope = labelTargetScope(wrongAlgebra.labels[0].target);
  requireControlTransfer(wrongAlgebra.controlTransfers.asPtr(), breaks[0]).target =
      ControlTarget(LoopControlTarget{loopScope});
  auto algebraResult = BindingVerifier::verify(input, zc::mv(wrongAlgebra));
  ZC_EXPECT(requireBinderInvariant(algebraResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto sourceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(sourceCandidate.is<BindingMetadataCandidate>());
  auto& wrongSource = sourceCandidate.get<BindingMetadataCandidate>();
  const auto& continueFact =
      requireControlTransfer(wrongSource.controlTransfers.asPtr(), continues[0]);
  requireControlTransfer(wrongSource.controlTransfers.asPtr(), breaks[0]).source =
      continueFact.source.clone();
  auto sourceResult = BindingVerifier::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(sourceResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto reorderedCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(reorderedCandidate.is<BindingMetadataCandidate>());
  auto& reordered = reorderedCandidate.get<BindingMetadataCandidate>();
  auto firstResolution = zc::mv(reordered.nodeBindings[0]);
  reordered.nodeBindings[0] = zc::mv(reordered.nodeBindings[1]);
  reordered.nodeBindings[1] = zc::mv(firstResolution);
  auto reorderedResult = BindingVerifier::verify(input, zc::mv(reordered));
  ZC_EXPECT(requireBinderInvariant(reorderedResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsForeignExplicitLabelIdentities") {
  ParsedSource localSource("module root;\nfun run() { local: while (true) { break local; } }\n"_zc);
  ParsedSource foreignSource(
      "module root;\nfun run() { foreign: while (true) { break foreign; } }\n"_zc);
  FrozenFixture localFixture(localSource, true);
  FrozenFixture foreignFixture(foreignSource, true);
  auto localInputResult = verify(localFixture);
  auto foreignInputResult = verify(foreignFixture);
  ZC_REQUIRE(localInputResult.is<VerifiedBindingInput>());
  ZC_REQUIRE(foreignInputResult.is<VerifiedBindingInput>());
  auto localInput = zc::mv(localInputResult.get<VerifiedBindingInput>());
  auto foreignInput = zc::mv(foreignInputResult.get<VerifiedBindingInput>());
  const auto localBreaks = nodesOfKind(localInput.tree(), ast::SyntaxKind::BreakStmt);
  const auto foreignBreaks = nodesOfKind(foreignInput.tree(), ast::SyntaxKind::BreakStmt);
  ZC_REQUIRE(localBreaks.size() == 1);
  ZC_REQUIRE(foreignBreaks.size() == 1);

  auto foreignFactCandidate = BindingBuilder::build(foreignInput, *foreignSource.diagnostics);
  auto localFactCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(foreignFactCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(localFactCandidate.is<BindingMetadataCandidate>());
  auto& foreignFact = foreignFactCandidate.get<BindingMetadataCandidate>();
  auto& localFact = localFactCandidate.get<BindingMetadataCandidate>();
  auto& foreignTarget =
      requireControlTransfer(foreignFact.controlTransfers.asPtr(), foreignBreaks[0])
          .target.get<ExplicitLabelControlTarget>();
  auto& localTarget = requireControlTransfer(localFact.controlTransfers.asPtr(), localBreaks[0])
                          .target.get<ExplicitLabelControlTarget>();
  localTarget.label = zc::mv(foreignTarget.label);
  auto factResult = BindingVerifier::verify(localInput, zc::mv(localFact));
  ZC_EXPECT(requireIdentityInvariant(factResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto foreignBindingCandidate = BindingBuilder::build(foreignInput, *foreignSource.diagnostics);
  auto localBindingCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(foreignBindingCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(localBindingCandidate.is<BindingMetadataCandidate>());
  auto& foreignBinding = foreignBindingCandidate.get<BindingMetadataCandidate>();
  auto& localBinding = localBindingCandidate.get<BindingMetadataCandidate>();
  auto& foreignBound = requireResolution(foreignBinding.nodeBindings.asPtr(), foreignBreaks[0])
                           .value.get<BoundLabelResolution>();
  auto& localBound = requireResolution(localBinding.nodeBindings.asPtr(), localBreaks[0])
                         .value.get<BoundLabelResolution>();
  localBound.label = zc::mv(foreignBound.label);
  auto bindingResult = BindingVerifier::verify(localInput, zc::mv(localBinding));
  ZC_EXPECT(requireIdentityInvariant(bindingResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto foreignTargetCandidate = BindingBuilder::build(foreignInput, *foreignSource.diagnostics);
  auto localTargetCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(foreignTargetCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(localTargetCandidate.is<BindingMetadataCandidate>());
  auto& foreignTargetBinding = foreignTargetCandidate.get<BindingMetadataCandidate>();
  auto& localTargetBinding = localTargetCandidate.get<BindingMetadataCandidate>();
  auto& donor = requireResolution(foreignTargetBinding.nodeBindings.asPtr(), foreignBreaks[0])
                    .value.get<BoundLabelResolution>();
  auto& recipient = requireResolution(localTargetBinding.nodeBindings.asPtr(), localBreaks[0])
                        .value.get<BoundLabelResolution>();
  recipient.target = zc::mv(donor.target);
  auto targetResult = BindingVerifier::verify(localInput, zc::mv(localTargetBinding));
  ZC_EXPECT(requireIdentityInvariant(targetResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("BindingVerifier.RejectsMalformedExplicitLabelFailures") {
  ParsedSource sourceFixture("module root;\nfun run() { while (true) { break missing; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto breaks = nodesOfKind(input.tree(), ast::SyntaxKind::BreakStmt);
  ZC_REQUIRE(breaks.size() == 1);

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  missing.nodeBindings.removeLast();
  missing.sourceFailures.removeLast();
  auto missingResult = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto diagnosticCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(diagnosticCandidate.is<BindingMetadataCandidate>());
  auto& wrongDiagnostic = diagnosticCandidate.get<BindingMetadataCandidate>();
  wrongDiagnostic.sourceFailures[0].diagnostic = BinderDiagnosticCode::ContinueTargetNotLoop;
  auto diagnosticResult = BindingVerifier::verify(input, zc::mv(wrongDiagnostic));
  ZC_EXPECT(requireBinderInvariant(diagnosticResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto primaryCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(primaryCandidate.is<BindingMetadataCandidate>());
  auto& wrongPrimary = primaryCandidate.get<BindingMetadataCandidate>();
  auto keyword =
      input.parsedModule().retainedTokenSpan(breaks[0], 0, ast::SyntaxKind::BreakKeyword);
  ZC_REQUIRE(keyword != zc::none);
  ZC_IF_SOME(source, keyword) { wrongPrimary.sourceFailures[0].primary = source.clone(); }
  auto primaryResult = BindingVerifier::verify(input, zc::mv(wrongPrimary));
  ZC_EXPECT(requireBinderInvariant(primaryResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto emitterCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(emitterCandidate.is<BindingMetadataCandidate>());
  auto& wrongEmitter = emitterCandidate.get<BindingMetadataCandidate>();
  wrongEmitter.sourceFailures[0].emitterOrdinal =
      (uint64_t(static_cast<uint8_t>(BinderEmitterSite::BodyBinding)) << 56) |
      (wrongEmitter.sourceFailures[0].emitterOrdinal & 0x00ffffffffffffffULL);
  auto emitterResult = BindingVerifier::verify(input, zc::mv(wrongEmitter));
  ZC_EXPECT(requireBinderInvariant(emitterResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto indexCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(indexCandidate.is<BindingMetadataCandidate>());
  auto& wrongIndex = indexCandidate.get<BindingMetadataCandidate>();
  requireResolution(wrongIndex.nodeBindings.asPtr(), breaks[0])
      .value.get<FailedBindingResolution>()
      .failureIndex = wrongIndex.sourceFailures.size();
  auto indexResult = BindingVerifier::verify(input, zc::mv(wrongIndex));
  ZC_EXPECT(requireBinderInvariant(indexResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto additionalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  auto& additional = additionalCandidate.get<BindingMetadataCandidate>();
  const auto& original = additional.sourceFailures[0];
  zc::Vector<BindingDiagnosticNoteRef> noNotes;
  additional.sourceFailures.add(BindingFailureRef{original.diagnostic, original.primary.clone(),
                                                  original.emitterOrdinal + 1, zc::mv(noNotes)});
  auto additionalResult = BindingVerifier::verify(input, zc::mv(additional));
  ZC_EXPECT(requireBinderInvariant(additionalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
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
  const auto duplicateNodeScope = additionalValue.nodeScopes[0];
  additionalValue.nodeScopes.add(duplicateNodeScope);
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

ZC_TEST("ClosureFreeVariables.PublishesDenseCapturableFactsAndNonCaptures") {
  ParsedSource sourceFixture(
      "module root;\n"
      "const moduleValue = 1;\n"
      "class Type {}\n"
      "fun helper() {}\n"
      "fun run(parameter: i32) {\n"
      "  let local = parameter;\n"
      "  const direct = fun(own: i32) {\n"
      "    local; parameter; own;\n"
      "    let closureLocal = own;\n"
      "    closureLocal; moduleValue; helper();\n"
      "  };\n"
      "  const empty = () => 0;\n"
      "  for (let item in [1]) { const fromLoop = () => item; }\n"
      "  match (1) { when matched => { const fromMatch = () => matched; } }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto functionExpressions = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto lambdas = nodesOfKind(input.tree(), ast::SyntaxKind::LambdaExpression);
  ZC_REQUIRE(functionExpressions.size() == 1);
  ZC_REQUIRE(lambdas.size() == 3);
  const auto direct = requireDefinitionAt(input, functionExpressions[0]);
  const auto empty = requireDefinitionAt(input, lambdas[0]);
  const auto fromLoop = requireDefinitionAt(input, lambdas[1]);
  const auto fromMatch = requireDefinitionAt(input, lambdas[2]);
  const auto parameter =
      requireNamedFrozenDefinition(input, "parameter"_zc, identity::DefinitionKind::Parameter);
  const auto local =
      requireNamedFrozenDefinition(input, "local"_zc, identity::DefinitionKind::Local);
  const auto own =
      requireNamedFrozenDefinition(input, "own"_zc, identity::DefinitionKind::Parameter);
  const auto closureLocal =
      requireNamedFrozenDefinition(input, "closureLocal"_zc, identity::DefinitionKind::Local);
  const auto item =
      requireNamedFrozenDefinition(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  const auto matched =
      requireNamedFrozenDefinition(input, "matched"_zc, identity::DefinitionKind::PatternBinding);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto facts = verified.get<VerifiedBindingOutput>().metadata.closureFreeVariables();
  ZC_REQUIRE(facts.size() == 4);
  expectCanonicalClosureFreeVariables(input, facts);

  const auto& directFact = requireClosureFreeVariable(facts, direct);
  ZC_REQUIRE(directFact.variables.size() == 2);
  const auto& parameterFact = requireFreeVariable(directFact, parameter);
  const auto& localFact = requireFreeVariable(directFact, local);
  const auto parameterSites =
      identifierExpressionsInSubtree(input.tree(), functionExpressions[0], "parameter"_zc);
  const auto localSites =
      identifierExpressionsInSubtree(input.tree(), functionExpressions[0], "local"_zc);
  ZC_REQUIRE(parameterSites.size() == 1);
  ZC_REQUIRE(localSites.size() == 1);
  ZC_REQUIRE(parameterFact.referenceSites.size() == 1);
  ZC_REQUIRE(localFact.referenceSites.size() == 1);
  ZC_EXPECT(parameterFact.referenceSites[0] == parameterSites[0]);
  ZC_EXPECT(localFact.referenceSites[0] == localSites[0]);
  for (const auto& variable : directFact.variables) {
    ZC_EXPECT(variable.target != own);
    ZC_EXPECT(variable.target != closureLocal);
  }

  ZC_EXPECT(requireClosureFreeVariable(facts, empty).variables.empty());
  const auto& loopFact = requireClosureFreeVariable(facts, fromLoop);
  const auto& matchFact = requireClosureFreeVariable(facts, fromMatch);
  ZC_REQUIRE(loopFact.variables.size() == 1);
  ZC_REQUIRE(matchFact.variables.size() == 1);
  ZC_EXPECT(loopFact.variables[0].target == item);
  ZC_EXPECT(matchFact.variables[0].target == matched);
  const auto itemSites = identifierExpressionsInSubtree(input.tree(), lambdas[1], "item"_zc);
  const auto matchedSites = identifierExpressionsInSubtree(input.tree(), lambdas[2], "matched"_zc);
  ZC_REQUIRE(itemSites.size() == 1);
  ZC_REQUIRE(matchedSites.size() == 1);
  ZC_REQUIRE(loopFact.variables[0].referenceSites.size() == 1);
  ZC_REQUIRE(matchFact.variables[0].referenceSites.size() == 1);
  ZC_EXPECT(loopFact.variables[0].referenceSites[0] == itemSites[0]);
  ZC_EXPECT(matchFact.variables[0].referenceSites[0] == matchedSites[0]);
}

ZC_TEST("ClosureFreeVariables.RejectsModuleOwnedPatternAndLocalReferences") {
  ParsedSource sourceFixture(
      "module root;\n"
      "for (let item in [1]) {\n"
      "  let local = item;\n"
      "  const itemClosure = () => item;\n"
      "  const localClosure = () => local;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto lambdas = nodesOfKind(input.tree(), ast::SyntaxKind::LambdaExpression);
  ZC_REQUIRE(lambdas.size() == 2);
  const auto item =
      requireNamedFrozenDefinition(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  const auto local =
      requireNamedFrozenDefinition(input, "local"_zc, identity::DefinitionKind::Local);
  const auto itemClosure = requireDefinitionAt(input, lambdas[0]);
  const auto localClosure = requireDefinitionAt(input, lambdas[1]);
  const auto itemSites = identifierExpressionsInSubtree(input.tree(), lambdas[0], "item"_zc);
  const auto localSites = identifierExpressionsInSubtree(input.tree(), lambdas[1], "local"_zc);
  ZC_REQUIRE(itemSites.size() == 1);
  ZC_REQUIRE(localSites.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 2);
  ZC_REQUIRE(value.closureFreeVariables.size() == 2);
  ZC_EXPECT(value.explicitClosureCaptures.empty());
  const auto& itemFact = requireDefinitionFact(value.definitions.asPtr(), item);
  const auto& localFact = requireDefinitionFact(value.definitions.asPtr(), local);
  ZC_EXPECT(itemFact.kind == identity::DefinitionKind::PatternBinding);
  ZC_EXPECT(itemFact.activation == DefinitionActivation::LoopPattern);
  ZC_EXPECT(localFact.kind == identity::DefinitionKind::Local);
  ZC_EXPECT(localFact.activation == DefinitionActivation::AfterInitializer);
  const auto& itemOwner = value.scopes[itemFact.declaringScope.index()].owner.value();
  const auto& localOwner = value.scopes[localFact.declaringScope.index()].owner.value();
  ZC_REQUIRE(itemOwner.is<ModuleScopeOwner>());
  ZC_REQUIRE(localOwner.is<ModuleScopeOwner>());
  ZC_EXPECT(itemOwner.get<ModuleScopeOwner>().module == input.module());
  ZC_EXPECT(localOwner.get<ModuleScopeOwner>().module == input.module());

  const ast::NodeId rejectedSites[] = {itemSites[0], localSites[0]};
  const identity::DefId rejectedTargets[] = {item, local};
  const identity::DefId closures[] = {itemClosure, localClosure};
  for (size_t index = 0; index < zc::size(rejectedSites); ++index) {
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), rejectedSites[index]);
    ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
    const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
    ZC_EXPECT(failureIndex == index);
    ZC_REQUIRE(failureIndex < value.sourceFailures.size());
    const auto& failure = value.sourceFailures[failureIndex];
    ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
    ZC_EXPECT((failure.emitterOrdinal >> 56) ==
              static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
    ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
              schemaPreorderOrdinal(input.tree(), rejectedSites[index]));
    ZC_EXPECT(static_cast<uint16_t>(failure.emitterOrdinal) == 0);
    ZC_EXPECT(failure.notes.empty());
    auto token = input.parsedModule().retainedTokenSpan(rejectedSites[index], 0,
                                                        ast::SyntaxKind::Identifier);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(span, token) { ZC_EXPECT(sameSpan(failure.primary, span)); }
    const auto& row =
        requireClosureFreeVariable(value.closureFreeVariables.asPtr(), closures[index]);
    ZC_EXPECT(row.closure == closures[index]);
    ZC_EXPECT(row.variables.empty());
    ZC_EXPECT(rejectedTargets[index] != closures[index]);
  }

  const auto allItemReferences = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(allItemReferences.size() == 2);
  ZC_EXPECT(allItemReferences[1] == itemSites[0]);
  const auto& directResolution =
      requireResolution(value.nodeBindings.asPtr(), allItemReferences[0]);
  ZC_REQUIRE(directResolution.value.is<BoundNameResolution>());
  const auto& directBound = directResolution.value.get<BoundNameResolution>();
  ZC_EXPECT(requireDefinitionTarget(directBound.bindingIdentity) == item);
  ZC_EXPECT(requireDefinitionTarget(directBound.canonicalTarget) == item);

  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(rejected.get<SourceRejected>().failures().size() == 2);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 2);
}

ZC_TEST("ClosureFreeVariables.PropagatesOriginalSitesAcrossNestedClosures") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(root: i32) {\n"
      "  const outer = (outerParam: i32) => {\n"
      "    let outerLocal = outerParam;\n"
      "    const inner = () => { root; root; outerParam; outerLocal; };\n"
      "  };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto lambdas = nodesOfKind(input.tree(), ast::SyntaxKind::LambdaExpression);
  ZC_REQUIRE(lambdas.size() == 2);
  const auto outer = requireDefinitionAt(input, lambdas[0]);
  const auto inner = requireDefinitionAt(input, lambdas[1]);
  const auto root =
      requireNamedFrozenDefinition(input, "root"_zc, identity::DefinitionKind::Parameter);
  const auto outerParameter =
      requireNamedFrozenDefinition(input, "outerParam"_zc, identity::DefinitionKind::Parameter);
  const auto outerLocal =
      requireNamedFrozenDefinition(input, "outerLocal"_zc, identity::DefinitionKind::Local);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto facts = verified.get<VerifiedBindingOutput>().metadata.closureFreeVariables();
  ZC_REQUIRE(facts.size() == 2);
  expectCanonicalClosureFreeVariables(input, facts);
  const auto& outerFact = requireClosureFreeVariable(facts, outer);
  const auto& innerFact = requireClosureFreeVariable(facts, inner);
  ZC_REQUIRE(outerFact.variables.size() == 1);
  ZC_REQUIRE(innerFact.variables.size() == 3);
  const auto& outerRoot = requireFreeVariable(outerFact, root);
  const auto& innerRoot = requireFreeVariable(innerFact, root);
  ZC_REQUIRE(requireFreeVariable(innerFact, outerParameter).referenceSites.size() == 1);
  ZC_REQUIRE(requireFreeVariable(innerFact, outerLocal).referenceSites.size() == 1);
  const auto originalSites = identifierExpressionsInSubtree(input.tree(), lambdas[1], "root"_zc);
  ZC_REQUIRE(originalSites.size() == 2);
  ZC_REQUIRE(outerRoot.referenceSites.size() == originalSites.size());
  ZC_REQUIRE(innerRoot.referenceSites.size() == originalSites.size());
  for (size_t index = 0; index < originalSites.size(); ++index) {
    ZC_EXPECT(outerRoot.referenceSites[index] == originalSites[index]);
    ZC_EXPECT(innerRoot.referenceSites[index] == originalSites[index]);
  }
}

ZC_TEST("ExplicitClosureCaptures.PublishSourceOrderedBindingsAndEmptyClauses") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(parameter: i32) {\n"
      "  let local = parameter;\n"
      "  const explicit = fun(own: i32) use [local, &parameter] {\n"
      "    local; parameter; own;\n"
      "  };\n"
      "  const empty = fun() use [] {};\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(closures.size() == 2);
  ZC_REQUIRE(captureItems.size() == 2);
  const auto explicitClosure = requireDefinitionAt(input, closures[0]);
  const auto emptyClosure = requireDefinitionAt(input, closures[1]);
  const auto local =
      requireNamedFrozenDefinition(input, "local"_zc, identity::DefinitionKind::Local);
  const auto parameter =
      requireNamedFrozenDefinition(input, "parameter"_zc, identity::DefinitionKind::Parameter);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  ZC_EXPECT(metadata.closureFreeVariables().size() == 0);
  ZC_REQUIRE(metadata.explicitClosureCaptures().size() == 2);
  const auto& explicitFact =
      requireExplicitClosureCapture(metadata.explicitClosureCaptures(), explicitClosure);
  const auto& emptyFact =
      requireExplicitClosureCapture(metadata.explicitClosureCaptures(), emptyClosure);
  ZC_REQUIRE(explicitFact.captures.size() == 2);
  ZC_EXPECT(emptyFact.captures.empty());
  ZC_EXPECT(explicitFact.captures[0].item == captureItems[0]);
  ZC_EXPECT(explicitFact.captures[0].target == local);
  ZC_EXPECT(explicitFact.captures[1].item == captureItems[1]);
  ZC_EXPECT(explicitFact.captures[1].target == parameter);
  ZC_EXPECT(input.tree().node(captureItems[0]).payload.words[ast::kCaptureItemModeWord] ==
            static_cast<uint32_t>(ast::CaptureMode::ByValue));
  ZC_EXPECT(input.tree().node(captureItems[1]).payload.words[ast::kCaptureItemModeWord] ==
            static_cast<uint32_t>(ast::CaptureMode::ByRef));
  for (size_t index = 0; index < captureItems.size(); ++index) {
    const auto& resolution = requireResolution(metadata.nodeBindings(), captureItems[index]);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    ZC_EXPECT(
        requireDefinitionTarget(resolution.value.get<BoundNameResolution>().bindingIdentity) ==
        explicitFact.captures[index].target);
  }
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ExplicitClosureCaptures.ResolveBeforeOwnParametersActivate") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(x: i32) {\n"
      "  const closure = fun(x: i32) use [x] { x; };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  const auto references = identifierExpressions(input.tree(), "x"_zc);
  ZC_REQUIRE(parameters.size() == 2);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(captureItems.size() == 1);
  ZC_REQUIRE(references.size() == 1);
  const auto outerParameter = requireDefinitionAt(input, parameters[0]);
  const auto innerParameter = requireDefinitionAt(input, parameters[1]);
  const auto closure = requireDefinitionAt(input, closures[0]);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("parameter activation capture fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };
  auto candidate = buildCandidate();
  auto verified = BindingVerifier::verify(input, zc::mv(candidate));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  ZC_EXPECT(fact.captures[0].target == outerParameter);
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(
      requireDefinitionTarget(captureResolution.value.get<BoundNameResolution>().bindingIdentity) ==
      outerParameter);
  const auto& bodyResolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(bodyResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                bodyResolution.value.get<BoundNameResolution>().bindingIdentity) == innerParameter);

  auto wrongInnerCapture = buildCandidate();
  auto& wrongBound = requireResolution(wrongInnerCapture.nodeBindings.asPtr(), captureItems[0])
                         .value.get<BoundNameResolution>();
  wrongBound.bindingIdentity = BindingTarget::definition(innerParameter);
  wrongBound.canonicalTarget = BindingTarget::definition(innerParameter);
  requireExplicitClosureCapture(wrongInnerCapture.explicitClosureCaptures.asPtr(), closure)
      .captures[0]
      .target = innerParameter;
  auto wrongInnerResult = BindingVerifier::verify(input, zc::mv(wrongInnerCapture));
  ZC_EXPECT(requireBinderInvariant(wrongInnerResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ExplicitClosureCaptures.ResolveBeforeOwnReceiverActivates") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(this: i32) {\n"
      "  const closure = fun(this: i32) use [this] { this; };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(parameters.size() == 2);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(captureItems.size() == 1);
  ZC_REQUIRE(receivers.size() == 1);
  const auto outerReceiver = requireDefinitionAt(input, parameters[0]);
  const auto innerReceiver = requireDefinitionAt(input, parameters[1]);
  const auto closure = requireDefinitionAt(input, closures[0]);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("receiver activation capture fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };
  auto candidate = buildCandidate();
  auto verified = BindingVerifier::verify(input, zc::mv(candidate));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  ZC_EXPECT(fact.captures[0].target == outerReceiver);
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(
      requireDefinitionTarget(captureResolution.value.get<BoundNameResolution>().bindingIdentity) ==
      outerReceiver);
  const auto& bodyResolution = requireResolution(metadata.nodeBindings(), receivers[0]);
  ZC_REQUIRE(bodyResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                bodyResolution.value.get<BoundNameResolution>().bindingIdentity) == innerReceiver);

  auto wrongInnerCapture = buildCandidate();
  auto& wrongBound = requireResolution(wrongInnerCapture.nodeBindings.asPtr(), captureItems[0])
                         .value.get<BoundNameResolution>();
  wrongBound.bindingIdentity = BindingTarget::definition(innerReceiver);
  wrongBound.canonicalTarget = BindingTarget::definition(innerReceiver);
  requireExplicitClosureCapture(wrongInnerCapture.explicitClosureCaptures.asPtr(), closure)
      .captures[0]
      .target = innerReceiver;
  auto wrongInnerResult = BindingVerifier::verify(input, zc::mv(wrongInnerCapture));
  ZC_EXPECT(requireBinderInvariant(wrongInnerResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingVerifier.RejectsWrongThisExpressionReceiverTarget") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(this: i32) {\n"
      "  const closure = fun(this: i32) use [this] { this; };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(parameters.size() == 2);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(captureItems.size() == 1);
  ZC_REQUIRE(receivers.size() == 1);
  const auto outerReceiver = requireDefinitionAt(input, parameters[0]);
  const auto innerReceiver = requireDefinitionAt(input, parameters[1]);
  const auto closure = requireDefinitionAt(input, closures[0]);
  ZC_EXPECT(outerReceiver != innerReceiver);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("nearest receiver verifier fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };

  auto baseline = buildCandidate();
  const auto& baselineRow =
      requireExplicitClosureCapture(baseline.explicitClosureCaptures.asPtr(), closure);
  ZC_REQUIRE(baselineRow.captures.size() == 1);
  ZC_EXPECT(baselineRow.captures[0].item == captureItems[0]);
  ZC_EXPECT(baselineRow.captures[0].target == outerReceiver);
  const auto& captureResolution = requireResolution(baseline.nodeBindings.asPtr(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  const auto& captureBound = captureResolution.value.get<BoundNameResolution>();
  ZC_EXPECT(requireDefinitionTarget(captureBound.bindingIdentity) == outerReceiver);
  ZC_EXPECT(requireDefinitionTarget(captureBound.canonicalTarget) == outerReceiver);
  const auto& receiverResolution = requireResolution(baseline.nodeBindings.asPtr(), receivers[0]);
  ZC_REQUIRE(receiverResolution.value.is<BoundNameResolution>());
  const auto& receiverBound = receiverResolution.value.get<BoundNameResolution>();
  ZC_EXPECT(requireDefinitionTarget(receiverBound.bindingIdentity) == innerReceiver);
  ZC_EXPECT(requireDefinitionTarget(receiverBound.canonicalTarget) == innerReceiver);
  auto verified = BindingVerifier::verify(input, zc::mv(baseline));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());

  auto wrongTarget = buildCandidate();
  auto& wrongReceiver = requireResolution(wrongTarget.nodeBindings.asPtr(), receivers[0])
                            .value.get<BoundNameResolution>();
  wrongReceiver.bindingIdentity = BindingTarget::definition(outerReceiver);
  wrongReceiver.canonicalTarget = BindingTarget::definition(outerReceiver);
  const auto& unchangedRow =
      requireExplicitClosureCapture(wrongTarget.explicitClosureCaptures.asPtr(), closure);
  ZC_REQUIRE(unchangedRow.captures.size() == 1);
  ZC_EXPECT(unchangedRow.captures[0].target == outerReceiver);
  auto wrongTargetResult = BindingVerifier::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(wrongTargetResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ExplicitClosureCaptures.PreferEarlierEnclosingBlockLocal") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(x: i32) {\n"
      "  {\n"
      "    let x = 1;\n"
      "    const closure = fun() use [x] { x; };\n"
      "  }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  const auto references = identifierExpressions(input.tree(), "x"_zc);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(captureItems.size() == 1);
  ZC_REQUIRE(references.size() == 1);
  const auto closure = requireDefinitionAt(input, closures[0]);
  const auto outerParameter =
      requireNamedFrozenDefinition(input, "x"_zc, identity::DefinitionKind::Parameter);
  const auto blockLocal =
      requireNamedFrozenDefinition(input, "x"_zc, identity::DefinitionKind::Local);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("enclosing local capture fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };
  auto candidate = buildCandidate();
  auto verified = BindingVerifier::verify(input, zc::mv(candidate));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  ZC_EXPECT(fact.captures[0].target == blockLocal);
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                captureResolution.value.get<BoundNameResolution>().bindingIdentity) == blockLocal);
  const auto& bodyResolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(bodyResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                bodyResolution.value.get<BoundNameResolution>().bindingIdentity) == blockLocal);

  auto wrongOuterCapture = buildCandidate();
  auto& wrongBound = requireResolution(wrongOuterCapture.nodeBindings.asPtr(), captureItems[0])
                         .value.get<BoundNameResolution>();
  wrongBound.bindingIdentity = BindingTarget::definition(outerParameter);
  wrongBound.canonicalTarget = BindingTarget::definition(outerParameter);
  requireExplicitClosureCapture(wrongOuterCapture.explicitClosureCaptures.asPtr(), closure)
      .captures[0]
      .target = outerParameter;
  auto wrongOuterResult = BindingVerifier::verify(input, zc::mv(wrongOuterCapture));
  ZC_EXPECT(requireBinderInvariant(wrongOuterResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ExplicitClosureCaptures.RejectLaterInitializerAndSiblingLocals") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  const laterClosure = fun() use [later] {};\n"
      "  let later = 1;\n"
      "  let recursive = fun() use [recursive] {};\n"
      "  { let sibling = 1; }\n"
      "  { const siblingClosure = fun() use [sibling] {}; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(closures.size() == 3);
  ZC_REQUIRE(captureItems.size() == 3);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.explicitClosureCaptures.size() == 3);
  ZC_REQUIRE(value.sourceFailures.size() == 3);
  for (size_t index = 0; index < captureItems.size(); ++index) {
    const auto& row = requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(),
                                                    requireDefinitionAt(input, closures[index]));
    ZC_EXPECT(row.captures.empty());
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), captureItems[index]);
    ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
    const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
    ZC_REQUIRE(failureIndex < value.sourceFailures.size());
    const auto& failure = value.sourceFailures[failureIndex];
    ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
    ZC_EXPECT((failure.emitterOrdinal >> 56) ==
              static_cast<uint64_t>(BinderEmitterSite::LabelAndClosure));
    ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
              schemaPreorderOrdinal(input.tree(), captureItems[index]));
    ZC_EXPECT(static_cast<uint16_t>(failure.emitterOrdinal) == 0);
    auto token =
        input.parsedModule().retainedTokenSpan(captureItems[index], 0, ast::SyntaxKind::Identifier);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(span, token) { ZC_EXPECT(sameSpan(failure.primary, span)); }
  }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("ExplicitClosureCaptures.EmptyClauseRejectsOuterReceiverReference") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun outer(this: i32) { const closure = fun() use [] { this; }; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(receivers.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.explicitClosureCaptures.size() == 1);
  const auto& row = requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(),
                                                  requireDefinitionAt(input, closures[0]));
  ZC_EXPECT(row.captures.empty());
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), receivers[0]);
  ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
  const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
  ZC_REQUIRE(failureIndex < value.sourceFailures.size());
  const auto& failure = value.sourceFailures[failureIndex];
  ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT((failure.emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
  ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
            schemaPreorderOrdinal(input.tree(), receivers[0]));
  ZC_EXPECT(static_cast<uint16_t>(failure.emitterOrdinal) == 0);
  auto token =
      input.parsedModule().retainedTokenSpan(receivers[0], 0, ast::SyntaxKind::ThisKeyword);
  ZC_REQUIRE(token != zc::none);
  ZC_IF_SOME(span, token) { ZC_EXPECT(sameSpan(failure.primary, span)); }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("ReceiverBinding.DoesNotLeakAcrossNamedFunctions") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun owner(this: i32) {}\n"
      "fun observer() { this; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(receivers.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), receivers[0]);
  ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
  const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
  ZC_REQUIRE(failureIndex < value.sourceFailures.size());
  const auto& failure = value.sourceFailures[failureIndex];
  ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT((failure.emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
  ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
            schemaPreorderOrdinal(input.tree(), receivers[0]));
  ZC_EXPECT(static_cast<uint16_t>(failure.emitterOrdinal) == 0);
  auto token =
      input.parsedModule().retainedTokenSpan(receivers[0], 0, ast::SyntaxKind::ThisKeyword);
  ZC_REQUIRE(token != zc::none);
  ZC_IF_SOME(span, token) { ZC_EXPECT(sameSpan(failure.primary, span)); }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("ClosureFreeVariables.InfersReceiverAcrossImplicitClosure") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun outer(this: i32) { const closure = fun() { this; }; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(parameters.size() == 1);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(receivers.size() == 1);
  const auto receiver = requireDefinitionAt(input, parameters[0]);
  const auto closure = requireDefinitionAt(input, closures[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  ZC_EXPECT(metadata.explicitClosureCaptures().size() == 0);
  ZC_REQUIRE(metadata.closureFreeVariables().size() == 1);
  const auto& row = requireClosureFreeVariable(metadata.closureFreeVariables(), closure);
  ZC_REQUIRE(row.variables.size() == 1);
  const auto& variable = requireFreeVariable(row, receiver);
  ZC_REQUIRE(variable.referenceSites.size() == 1);
  ZC_EXPECT(variable.referenceSites[0] == receivers[0]);
  const auto& resolution = requireResolution(metadata.nodeBindings(), receivers[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(resolution.value.get<BoundNameResolution>().bindingIdentity) ==
            receiver);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ExplicitClosureCaptures.BindReceiverAndThisExpression") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun make(this: i32) { const closure = fun() use [this] { this; }; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(captureItems.size() == 1);
  ZC_REQUIRE(receivers.size() == 1);
  ZC_REQUIRE(parameters.size() == 1);
  const auto closure = requireDefinitionAt(input, closures[0]);
  const auto receiver = requireDefinitionAt(input, parameters[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  ZC_EXPECT(fact.captures[0].target == receiver);
  const auto& receiverResolution = requireResolution(metadata.nodeBindings(), receivers[0]);
  ZC_REQUIRE(receiverResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                receiverResolution.value.get<BoundNameResolution>().bindingIdentity) == receiver);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ReceiverBinding.BindsAttributedReceiverAndPreservesNameToken") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun make(#[zom::param::move] this: i32) {\n"
      "  const closure = fun() use [this] { this; };\n"
      "}\n"_zc);
  auto snapshot = sourceFixture.snapshot();
  auto expectedReceiverToken = snapshot.span(42, 46);
  ZC_REQUIRE(expectedReceiverToken != zc::none);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(parameters.size() == 1);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(captureItems.size() == 1);
  ZC_REQUIRE(receivers.size() == 1);
  const auto& parameterSyntax = input.tree().node(parameters[0]);
  ZC_EXPECT(input.tree().ident(ast::IdentId(
                parameterSyntax.payload.words[ast::kFunctionParameterDeclNameWord])) == "this"_zc);
  auto receiverName =
      input.parsedModule().functionParameterNameSpan(parameters[0], ast::SyntaxKind::ThisKeyword);
  ZC_REQUIRE(receiverName != zc::none);
  ZC_IF_SOME(span, receiverName) {
    ZC_EXPECT(sameSpan(span, ZC_ASSERT_NONNULL(expectedReceiverToken)));
  }
  size_t retainedThisTokens = 0;
  for (uint32_t ordinal = 0; ordinal < 32; ++ordinal) {
    auto token = input.parsedModule().retainedTokenSpan(parameters[0], ordinal,
                                                        ast::SyntaxKind::ThisKeyword);
    ZC_IF_SOME(span, token) {
      ++retainedThisTokens;
      ZC_EXPECT(sameSpan(span, ZC_ASSERT_NONNULL(expectedReceiverToken)));
    }
  }
  ZC_EXPECT(retainedThisTokens == 1);
  const auto receiver = requireDefinitionAt(input, parameters[0]);
  const auto closure = requireDefinitionAt(input, closures[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  ZC_EXPECT(fact.captures[0].target == receiver);
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                captureResolution.value.get<BoundNameResolution>().bindingIdentity) == receiver);
  const auto& receiverResolution = requireResolution(metadata.nodeBindings(), receivers[0]);
  ZC_REQUIRE(receiverResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                receiverResolution.value.get<BoundNameResolution>().bindingIdentity) == receiver);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ReceiverBinding.DoesNotClassifyAttributedOrdinaryParameter") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(#[this::marker] value: i32) { value; }\n"_zc);
  auto snapshot = sourceFixture.snapshot();
  auto expectedAttributeToken = snapshot.span(23, 27);
  auto expectedParameterToken = snapshot.span(37, 42);
  ZC_REQUIRE(expectedAttributeToken != zc::none);
  ZC_REQUIRE(expectedParameterToken != zc::none);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto references = identifierExpressions(input.tree(), "value"_zc);
  ZC_REQUIRE(parameters.size() == 1);
  ZC_REQUIRE(references.size() == 1);
  const auto& parameterSyntax = input.tree().node(parameters[0]);
  ZC_EXPECT(input.tree().ident(ast::IdentId(
                parameterSyntax.payload.words[ast::kFunctionParameterDeclNameWord])) == "value"_zc);
  ZC_EXPECT(input.parsedModule().functionParameterNameSpan(
                parameters[0], ast::SyntaxKind::ThisKeyword) == zc::none);
  auto parameterName =
      input.parsedModule().functionParameterNameSpan(parameters[0], ast::SyntaxKind::Identifier);
  ZC_REQUIRE(parameterName != zc::none);
  ZC_IF_SOME(span, parameterName) {
    ZC_EXPECT(sameSpan(span, ZC_ASSERT_NONNULL(expectedParameterToken)));
  }
  size_t retainedThisTokens = 0;
  for (uint32_t ordinal = 0; ordinal < 32; ++ordinal) {
    auto token = input.parsedModule().retainedTokenSpan(parameters[0], ordinal,
                                                        ast::SyntaxKind::ThisKeyword);
    ZC_IF_SOME(span, token) {
      ++retainedThisTokens;
      ZC_EXPECT(sameSpan(span, ZC_ASSERT_NONNULL(expectedAttributeToken)));
    }
  }
  ZC_EXPECT(retainedThisTokens == 1);
  const auto parameter = requireDefinitionAt(input, parameters[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& resolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(resolution.value.get<BoundNameResolution>().bindingIdentity) ==
            parameter);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ReceiverBinding.BindsReceiverAfterStackedOuterAttributes") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun make(#[audit::first] #[zom::param::move] this: i32) {\n"
      "  const closure = fun() use [this] { this; };\n"
      "}\n"_zc);
  auto snapshot = sourceFixture.snapshot();
  auto expectedReceiverToken = snapshot.span(58, 62);
  ZC_REQUIRE(expectedReceiverToken != zc::none);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(parameters.size() == 1);
  ZC_REQUIRE(closures.size() == 1);
  ZC_REQUIRE(captureItems.size() == 1);
  ZC_REQUIRE(receivers.size() == 1);
  auto receiverName =
      input.parsedModule().functionParameterNameSpan(parameters[0], ast::SyntaxKind::ThisKeyword);
  ZC_REQUIRE(receiverName != zc::none);
  ZC_IF_SOME(span, receiverName) {
    ZC_EXPECT(sameSpan(span, ZC_ASSERT_NONNULL(expectedReceiverToken)));
  }
  const auto receiver = requireDefinitionAt(input, parameters[0]);
  const auto closure = requireDefinitionAt(input, closures[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  ZC_EXPECT(fact.captures[0].target == receiver);
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                captureResolution.value.get<BoundNameResolution>().bindingIdentity) == receiver);
  const auto& receiverResolution = requireResolution(metadata.nodeBindings(), receivers[0]);
  ZC_REQUIRE(receiverResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(requireDefinitionTarget(
                receiverResolution.value.get<BoundNameResolution>().bindingIdentity) == receiver);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ReceiverBinding.RejectsMissingAndDuplicateReceivers") {
  ParsedSource missingSource("module root;\nfun run() { this; }\n"_zc);
  FrozenFixture missingFixture(missingSource, true);
  auto missingInputResult = verify(missingFixture);
  ZC_REQUIRE(missingInputResult.is<VerifiedBindingInput>());
  auto missingInput = zc::mv(missingInputResult.get<VerifiedBindingInput>());
  const auto receiverReferences = nodesOfKind(missingInput.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(receiverReferences.size() == 1);
  auto missingCandidate = BindingBuilder::build(missingInput, *missingSource.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missing.sourceFailures.size() == 1);
  ZC_EXPECT(missing.sourceFailures[0].diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT((missing.sourceFailures[0].emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
  auto missingToken = missingInput.parsedModule().retainedTokenSpan(receiverReferences[0], 0,
                                                                    ast::SyntaxKind::ThisKeyword);
  ZC_REQUIRE(missingToken != zc::none);
  ZC_IF_SOME(span, missingToken) { ZC_EXPECT(sameSpan(missing.sourceFailures[0].primary, span)); }
  auto missingRejected = BindingVerifier::verify(missingInput, zc::mv(missing));
  ZC_REQUIRE(missingRejected.is<SourceRejected>());

  ParsedSource duplicateSource(
      "module root;\n"
      "fun run(this: i32, this: i32) { this; }\n"_zc);
  FrozenFixture duplicateFixture(duplicateSource, true);
  auto duplicateInputResult = verify(duplicateFixture);
  ZC_REQUIRE(duplicateInputResult.is<VerifiedBindingInput>());
  auto duplicateInput = zc::mv(duplicateInputResult.get<VerifiedBindingInput>());
  const auto duplicateParameters =
      nodesOfKind(duplicateInput.tree(), ast::SyntaxKind::FunctionParameterDecl);
  ZC_REQUIRE(duplicateParameters.size() == 2);
  auto duplicateCandidate = BindingBuilder::build(duplicateInput, *duplicateSource.diagnostics);
  ZC_REQUIRE(duplicateCandidate.is<BindingMetadataCandidate>());
  auto& duplicate = duplicateCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(duplicate.sourceFailures.size() == 1);
  ZC_EXPECT(duplicate.sourceFailures[0].diagnostic == BinderDiagnosticCode::RedeclareParameter);
  ZC_REQUIRE(duplicate.sourceFailures[0].notes.size() == 1);
  ZC_EXPECT(duplicate.sourceFailures[0].notes[0].diagnostic ==
            BinderDiagnosticCode::PreviousDeclarationHere);
  auto firstReceiverToken = duplicateInput.parsedModule().retainedTokenSpan(
      duplicateParameters[0], 0, ast::SyntaxKind::ThisKeyword);
  auto secondReceiverToken = duplicateInput.parsedModule().retainedTokenSpan(
      duplicateParameters[1], 0, ast::SyntaxKind::ThisKeyword);
  ZC_REQUIRE(firstReceiverToken != zc::none);
  ZC_REQUIRE(secondReceiverToken != zc::none);
  ZC_IF_SOME(span, firstReceiverToken) {
    ZC_EXPECT(sameSpan(duplicate.sourceFailures[0].notes[0].source, span));
  }
  ZC_IF_SOME(span, secondReceiverToken) {
    ZC_EXPECT(sameSpan(duplicate.sourceFailures[0].primary, span));
  }
  for (const auto& scope : duplicate.scopes) {
    for (const auto& binding : scope.bindings) {
      ZC_EXPECT(binding.name.name().text() != "this"_zc);
    }
  }
  auto duplicateRejected = BindingVerifier::verify(duplicateInput, zc::mv(duplicate));
  ZC_REQUIRE(duplicateRejected.is<SourceRejected>());
}

ZC_TEST("BindingVerifier.RejectsMalformedDuplicateReceiverFailures") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(this: i32, this: i32, this: i32) {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  ZC_REQUIRE(parameters.size() == 3);
  zc::Vector<identity::SourceSpan> receiverTokens;
  for (const auto parameter : parameters) {
    auto token =
        input.parsedModule().functionParameterNameSpan(parameter, ast::SyntaxKind::ThisKeyword);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(span, token) { receiverTokens.add(span.clone()); }
  }
  ZC_REQUIRE(receiverTokens.size() == 3);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("duplicate receiver mutation fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };
  auto baseline = buildCandidate();
  ZC_REQUIRE(baseline.sourceFailures.size() == 2);
  for (size_t index = 0; index < baseline.sourceFailures.size(); ++index) {
    const auto& failure = baseline.sourceFailures[index];
    ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::RedeclareParameter);
    ZC_EXPECT((failure.emitterOrdinal >> 56) ==
              static_cast<uint64_t>(BinderEmitterSite::ModuleSkeleton));
    ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
              schemaPreorderOrdinal(input.tree(), parameters[index + 1]));
    ZC_EXPECT(static_cast<uint16_t>(failure.emitterOrdinal) == 0);
    ZC_EXPECT(sameSpan(failure.primary, receiverTokens[index + 1]));
    ZC_REQUIRE(failure.notes.size() == 1);
    ZC_EXPECT(failure.notes[0].diagnostic == BinderDiagnosticCode::PreviousDeclarationHere);
    ZC_EXPECT(sameSpan(failure.notes[0].source, receiverTokens[0]));
  }
  auto baselineRejected = BindingVerifier::verify(input, zc::mv(baseline));
  ZC_REQUIRE(baselineRejected.is<SourceRejected>());

  auto missingFailure = buildCandidate();
  missingFailure.sourceFailures.removeLast();
  auto missingFailureResult = BindingVerifier::verify(input, zc::mv(missingFailure));
  ZC_EXPECT(requireBinderInvariant(missingFailureResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto wrongDiagnostic = buildCandidate();
  wrongDiagnostic.sourceFailures[0].diagnostic = BinderDiagnosticCode::UndefinedIdentifier;
  auto wrongDiagnosticResult = BindingVerifier::verify(input, zc::mv(wrongDiagnostic));
  ZC_EXPECT(requireBinderInvariant(wrongDiagnosticResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingNote = buildCandidate();
  missingNote.sourceFailures[0].notes.removeLast();
  auto missingNoteResult = BindingVerifier::verify(input, zc::mv(missingNote));
  ZC_EXPECT(requireBinderInvariant(missingNoteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongNoteDiagnostic = buildCandidate();
  wrongNoteDiagnostic.sourceFailures[0].notes[0].diagnostic =
      BinderDiagnosticCode::UndefinedIdentifier;
  auto wrongNoteDiagnosticResult = BindingVerifier::verify(input, zc::mv(wrongNoteDiagnostic));
  ZC_EXPECT(requireBinderInvariant(wrongNoteDiagnosticResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongEmitterSite = buildCandidate();
  wrongEmitterSite.sourceFailures[0].emitterOrdinal =
      (uint64_t(static_cast<uint8_t>(BinderEmitterSite::BodyBinding)) << 56) |
      (wrongEmitterSite.sourceFailures[0].emitterOrdinal & 0x00ffffffffffffffULL);
  auto wrongEmitterSiteResult = BindingVerifier::verify(input, zc::mv(wrongEmitterSite));
  ZC_EXPECT(wrongEmitterSiteResult.is<InvariantRejected>());

  auto wrongSchemaOrdinal = buildCandidate();
  wrongSchemaOrdinal.sourceFailures[0].emitterOrdinal ^= uint64_t{1} << 16;
  auto wrongSchemaOrdinalResult = BindingVerifier::verify(input, zc::mv(wrongSchemaOrdinal));
  ZC_EXPECT(wrongSchemaOrdinalResult.is<InvariantRejected>());

  auto wrongLocalOrdinal = buildCandidate();
  wrongLocalOrdinal.sourceFailures[0].emitterOrdinal =
      (wrongLocalOrdinal.sourceFailures[0].emitterOrdinal & 0xffffffffffff0000ULL) | 1;
  auto wrongLocalOrdinalResult = BindingVerifier::verify(input, zc::mv(wrongLocalOrdinal));
  ZC_EXPECT(requireBinderInvariant(wrongLocalOrdinalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongPrimary = buildCandidate();
  wrongPrimary.sourceFailures[0].primary = receiverTokens[0].clone();
  auto wrongPrimaryResult = BindingVerifier::verify(input, zc::mv(wrongPrimary));
  ZC_EXPECT(requireBinderInvariant(wrongPrimaryResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongNoteSource = buildCandidate();
  wrongNoteSource.sourceFailures[0].notes[0].source = receiverTokens[1].clone();
  auto wrongNoteSourceResult = BindingVerifier::verify(input, zc::mv(wrongNoteSource));
  ZC_EXPECT(requireBinderInvariant(wrongNoteSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedFailures = buildCandidate();
  auto displacedFailure = zc::mv(reorderedFailures.sourceFailures[0]);
  reorderedFailures.sourceFailures[0] = zc::mv(reorderedFailures.sourceFailures[1]);
  reorderedFailures.sourceFailures[1] = zc::mv(displacedFailure);
  auto reorderedFailuresResult = BindingVerifier::verify(input, zc::mv(reorderedFailures));
  ZC_EXPECT(requireBinderInvariant(reorderedFailuresResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("ExplicitClosureCaptures.EnforcesDeclaredCaptureExhaustiveness") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(first: i32, second: i32) {\n"
      "  const empty = fun() use [] { first; };\n"
      "  const partial = fun() use [first] { first; second; };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  const auto secondReferences = identifierExpressions(input.tree(), "second"_zc);
  ZC_REQUIRE(closures.size() == 2);
  ZC_REQUIRE(captureItems.size() == 1);
  ZC_REQUIRE(firstReferences.size() == 2);
  ZC_REQUIRE(secondReferences.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.explicitClosureCaptures.size() == 2);
  const auto& empty = requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(),
                                                    requireDefinitionAt(input, closures[0]));
  const auto& partial = requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(),
                                                      requireDefinitionAt(input, closures[1]));
  ZC_EXPECT(empty.captures.empty());
  ZC_REQUIRE(partial.captures.size() == 1);
  ZC_EXPECT(partial.captures[0].item == captureItems[0]);
  ZC_REQUIRE(value.sourceFailures.size() == 2);
  for (const auto& failure : value.sourceFailures) {
    ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
    ZC_EXPECT((failure.emitterOrdinal >> 56) ==
              static_cast<uint64_t>(BinderEmitterSite::BodyBinding));
  }
  ZC_REQUIRE(requireResolution(value.nodeBindings.asPtr(), captureItems[0])
                 .value.is<BoundNameResolution>());
  ZC_REQUIRE(requireResolution(value.nodeBindings.asPtr(), firstReferences[0])
                 .value.is<FailedBindingResolution>());
  ZC_REQUIRE(requireResolution(value.nodeBindings.asPtr(), firstReferences[1])
                 .value.is<BoundNameResolution>());
  ZC_REQUIRE(requireResolution(value.nodeBindings.asPtr(), secondReferences[0])
                 .value.is<FailedBindingResolution>());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("ExplicitClosureCaptures.EnforcesExhaustivenessForNestedCaptureItems") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(value: i32) {\n"
      "  const outer = fun() use [] {\n"
      "    const inner = fun() use [value] {};\n"
      "  };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(closures.size() == 2);
  ZC_REQUIRE(captureItems.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.explicitClosureCaptures.size() == 2);
  const auto& outer = requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(),
                                                    requireDefinitionAt(input, closures[0]));
  const auto& inner = requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(),
                                                    requireDefinitionAt(input, closures[1]));
  ZC_EXPECT(outer.captures.empty());
  ZC_EXPECT(inner.captures.empty());
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), captureItems[0]);
  ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
  const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
  ZC_REQUIRE(failureIndex < value.sourceFailures.size());
  const auto& failure = value.sourceFailures[failureIndex];
  ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT((failure.emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::LabelAndClosure));
  ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
            schemaPreorderOrdinal(input.tree(), captureItems[0]));
  ZC_EXPECT(static_cast<uint16_t>(failure.emitterOrdinal) == 0);
  auto token =
      input.parsedModule().retainedTokenSpan(captureItems[0], 0, ast::SyntaxKind::Identifier);
  ZC_REQUIRE(token != zc::none);
  ZC_IF_SOME(span, token) { ZC_EXPECT(sameSpan(failure.primary, span)); }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("ExplicitClosureCaptures.RejectsUndefinedNonCapturableAndMissingReceiverItems") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class helper {}\n"
      "fun helper() {}\n"
      "fun run() {\n"
      "  const missingCapture = fun() use [missing] {};\n"
      "  const functionCapture = fun() use [helper] {};\n"
      "  const receiverCapture = fun() use [this] {};\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(captureItems.size() == 3);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.explicitClosureCaptures.size() == 3);
  ZC_REQUIRE(value.sourceFailures.size() == 3);
  for (size_t index = 0; index < captureItems.size(); ++index) {
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), captureItems[index]);
    ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
    const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
    ZC_REQUIRE(failureIndex < value.sourceFailures.size());
    const auto& failure = value.sourceFailures[failureIndex];
    ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
    ZC_EXPECT((failure.emitterOrdinal >> 56) ==
              static_cast<uint64_t>(BinderEmitterSite::LabelAndClosure));
    ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
              schemaPreorderOrdinal(input.tree(), captureItems[index]));
    const auto mode = static_cast<ast::CaptureMode>(
        input.tree().node(captureItems[index]).payload.words[ast::kCaptureItemModeWord]);
    const auto tokenKind =
        mode == ast::CaptureMode::This ? ast::SyntaxKind::ThisKeyword : ast::SyntaxKind::Identifier;
    auto source = input.parsedModule().retainedTokenSpan(captureItems[index], 0, tokenKind);
    ZC_REQUIRE(source != zc::none);
    ZC_IF_SOME(span, source) { ZC_EXPECT(sameSpan(failure.primary, span)); }
  }
  for (const auto& row : value.explicitClosureCaptures) { ZC_EXPECT(row.captures.empty()); }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("ExplicitClosureCaptures.RejectsModuleOwnedPatternAndLocalItems") {
  ParsedSource sourceFixture(
      "module root;\n"
      "for (let item in [1]) {\n"
      "  let local = item;\n"
      "  const itemClosure = fun() use [item] {};\n"
      "  const localClosure = fun() use [local] {};\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closureNodes = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(closureNodes.size() == 2);
  ZC_REQUIRE(captureItems.size() == 2);
  const auto item =
      requireNamedFrozenDefinition(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  const auto local =
      requireNamedFrozenDefinition(input, "local"_zc, identity::DefinitionKind::Local);
  const identity::DefId closures[] = {requireDefinitionAt(input, closureNodes[0]),
                                      requireDefinitionAt(input, closureNodes[1])};

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 2);
  ZC_REQUIRE(value.explicitClosureCaptures.size() == 2);
  ZC_EXPECT(value.closureFreeVariables.empty());
  const auto& itemFact = requireDefinitionFact(value.definitions.asPtr(), item);
  const auto& localFact = requireDefinitionFact(value.definitions.asPtr(), local);
  ZC_EXPECT(itemFact.kind == identity::DefinitionKind::PatternBinding);
  ZC_EXPECT(itemFact.activation == DefinitionActivation::LoopPattern);
  ZC_EXPECT(localFact.kind == identity::DefinitionKind::Local);
  ZC_EXPECT(localFact.activation == DefinitionActivation::AfterInitializer);
  const auto& itemOwner = value.scopes[itemFact.declaringScope.index()].owner.value();
  const auto& localOwner = value.scopes[localFact.declaringScope.index()].owner.value();
  ZC_REQUIRE(itemOwner.is<ModuleScopeOwner>());
  ZC_REQUIRE(localOwner.is<ModuleScopeOwner>());
  ZC_EXPECT(itemOwner.get<ModuleScopeOwner>().module == input.module());
  ZC_EXPECT(localOwner.get<ModuleScopeOwner>().module == input.module());

  const identity::DefId rejectedTargets[] = {item, local};
  for (size_t index = 0; index < captureItems.size(); ++index) {
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), captureItems[index]);
    ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
    const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
    ZC_EXPECT(failureIndex == index);
    ZC_REQUIRE(failureIndex < value.sourceFailures.size());
    const auto& failure = value.sourceFailures[failureIndex];
    ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::UndefinedIdentifier);
    ZC_EXPECT((failure.emitterOrdinal >> 56) ==
              static_cast<uint64_t>(BinderEmitterSite::LabelAndClosure));
    ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
              schemaPreorderOrdinal(input.tree(), captureItems[index]));
    ZC_EXPECT(static_cast<uint16_t>(failure.emitterOrdinal) == 0);
    ZC_EXPECT(failure.notes.empty());
    auto token =
        input.parsedModule().retainedTokenSpan(captureItems[index], 0, ast::SyntaxKind::Identifier);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(span, token) { ZC_EXPECT(sameSpan(failure.primary, span)); }
    const auto& row =
        requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(), closures[index]);
    ZC_EXPECT(row.closure == closures[index]);
    ZC_EXPECT(row.captures.empty());
    const auto& closureSyntax = input.tree().node(closureNodes[index]);
    const ast::NodeId captureList(
        closureSyntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
    ZC_EXPECT(row.captureList == captureList);
    auto rowSource = input.parsedModule().spanFor(input.tree().node(captureList).range);
    ZC_REQUIRE(rowSource != zc::none);
    ZC_IF_SOME(span, rowSource) { ZC_EXPECT(sameSpan(row.source, span)); }
    ZC_EXPECT(rejectedTargets[index] != closures[index]);
  }

  const auto directItemReferences = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(directItemReferences.size() == 1);
  const auto& directResolution =
      requireResolution(value.nodeBindings.asPtr(), directItemReferences[0]);
  ZC_REQUIRE(directResolution.value.is<BoundNameResolution>());
  const auto& directBound = directResolution.value.get<BoundNameResolution>();
  ZC_EXPECT(requireDefinitionTarget(directBound.bindingIdentity) == item);
  ZC_EXPECT(requireDefinitionTarget(directBound.canonicalTarget) == item);

  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(rejected.get<SourceRejected>().failures().size() == 2);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 2);
}

ZC_TEST("ExplicitClosureCaptures.ReportsWrongNamespaceAtExactToken") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class TypeOnly {}\n"
      "fun run() { const closure = fun() use [TypeOnly] {}; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(captureItems.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.explicitClosureCaptures.size() == 1);
  ZC_EXPECT(value.explicitClosureCaptures[0].captures.empty());
  const auto& resolution = requireResolution(value.nodeBindings.asPtr(), captureItems[0]);
  ZC_REQUIRE(resolution.value.is<FailedBindingResolution>());
  const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
  ZC_REQUIRE(failureIndex < value.sourceFailures.size());
  const auto& failure = value.sourceFailures[failureIndex];
  ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::SymbolNamespaceMismatch);
  ZC_EXPECT((failure.emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::LabelAndClosure));
  ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
            schemaPreorderOrdinal(input.tree(), captureItems[0]));
  ZC_EXPECT(static_cast<uint16_t>(failure.emitterOrdinal) == 0);
  auto token =
      input.parsedModule().retainedTokenSpan(captureItems[0], 0, ast::SyntaxKind::Identifier);
  ZC_REQUIRE(token != zc::none);
  ZC_IF_SOME(span, token) { ZC_EXPECT(sameSpan(failure.primary, span)); }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("ExplicitClosureCaptures.ReportsDuplicateTargetsAtExactTokens") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(value: i32) {\n"
      "  const closure = fun() use [value, &value] { value; };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(captureItems.size() == 2);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.explicitClosureCaptures.size() == 1);
  ZC_REQUIRE(value.explicitClosureCaptures[0].captures.size() == 2);
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  const auto& failure = value.sourceFailures[0];
  ZC_EXPECT(failure.diagnostic == BinderDiagnosticCode::DuplicateIdentifier);
  ZC_EXPECT((failure.emitterOrdinal >> 56) ==
            static_cast<uint64_t>(BinderEmitterSite::LabelAndClosure));
  ZC_EXPECT(((failure.emitterOrdinal >> 16) & UINT32_MAX) ==
            schemaPreorderOrdinal(input.tree(), captureItems[1]));
  ZC_REQUIRE(failure.notes.size() == 1);
  ZC_EXPECT(failure.notes[0].diagnostic == BinderDiagnosticCode::PreviousDeclarationHere);
  auto first =
      input.parsedModule().retainedTokenSpan(captureItems[0], 0, ast::SyntaxKind::Identifier);
  auto second =
      input.parsedModule().retainedTokenSpan(captureItems[1], 1, ast::SyntaxKind::Identifier);
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(second != zc::none);
  ZC_IF_SOME(span, first) { ZC_EXPECT(sameSpan(failure.notes[0].source, span)); }
  ZC_IF_SOME(span, second) { ZC_EXPECT(sameSpan(failure.primary, span)); }
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
}

ZC_TEST("BindingVerifier.RejectsMalformedExplicitCaptureFailures") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(value: i32) {\n"
      "  const missingClosure = fun() use [missing] {};\n"
      "  const duplicateClosure = fun() use [value, &value] {};\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(captureItems.size() == 3);
  auto missingToken =
      input.parsedModule().retainedTokenSpan(captureItems[0], 0, ast::SyntaxKind::Identifier);
  auto firstDuplicateToken =
      input.parsedModule().retainedTokenSpan(captureItems[1], 0, ast::SyntaxKind::Identifier);
  auto secondDuplicateToken =
      input.parsedModule().retainedTokenSpan(captureItems[2], 1, ast::SyntaxKind::Identifier);
  ZC_REQUIRE(missingToken != zc::none);
  ZC_REQUIRE(firstDuplicateToken != zc::none);
  ZC_REQUIRE(secondDuplicateToken != zc::none);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("explicit capture failure mutation fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };
  const auto missingFailureIndex = [&](BindingMetadataCandidate& candidate) -> size_t {
    auto& resolution = requireResolution(candidate.nodeBindings.asPtr(), captureItems[0]).value;
    if (!resolution.is<FailedBindingResolution>()) {
      ZC_FAIL_REQUIRE("missing capture must publish a failed resolution");
    }
    return resolution.get<FailedBindingResolution>().failureIndex;
  };
  const auto duplicateFailureIndex = [&](const BindingMetadataCandidate& candidate) -> size_t {
    for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
      if (candidate.sourceFailures[index].diagnostic == BinderDiagnosticCode::DuplicateIdentifier) {
        return index;
      }
    }
    ZC_FAIL_REQUIRE("duplicate capture failure is missing");
  };

  auto wrongFailedSite = buildCandidate();
  auto& failedSite = wrongFailedSite.sourceFailures[missingFailureIndex(wrongFailedSite)];
  failedSite.emitterOrdinal =
      (uint64_t(static_cast<uint8_t>(BinderEmitterSite::BodyBinding)) << 56) |
      (failedSite.emitterOrdinal & 0x00ffffffffffffffULL);
  auto wrongFailedSiteResult = BindingVerifier::verify(input, zc::mv(wrongFailedSite));
  ZC_EXPECT(requireBinderInvariant(wrongFailedSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongFailedSchemaOrdinal = buildCandidate();
  auto& failedSchema =
      wrongFailedSchemaOrdinal.sourceFailures[missingFailureIndex(wrongFailedSchemaOrdinal)];
  failedSchema.emitterOrdinal ^= uint64_t{1} << 16;
  auto wrongFailedSchemaResult = BindingVerifier::verify(input, zc::mv(wrongFailedSchemaOrdinal));
  ZC_EXPECT(requireBinderInvariant(wrongFailedSchemaResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongFailedLocalOrdinal = buildCandidate();
  auto& failedLocal =
      wrongFailedLocalOrdinal.sourceFailures[missingFailureIndex(wrongFailedLocalOrdinal)];
  failedLocal.emitterOrdinal = (failedLocal.emitterOrdinal & 0xffffffffffff0000ULL) | 1;
  auto wrongFailedLocalResult = BindingVerifier::verify(input, zc::mv(wrongFailedLocalOrdinal));
  ZC_EXPECT(requireBinderInvariant(wrongFailedLocalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongFailedPrimary = buildCandidate();
  auto& failedPrimary = wrongFailedPrimary.sourceFailures[missingFailureIndex(wrongFailedPrimary)];
  ZC_IF_SOME(span, firstDuplicateToken) { failedPrimary.primary = span.clone(); }
  auto wrongFailedPrimaryResult = BindingVerifier::verify(input, zc::mv(wrongFailedPrimary));
  ZC_EXPECT(requireBinderInvariant(wrongFailedPrimaryResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongDuplicatePrimary = buildCandidate();
  auto& duplicatePrimary =
      wrongDuplicatePrimary.sourceFailures[duplicateFailureIndex(wrongDuplicatePrimary)];
  ZC_IF_SOME(span, firstDuplicateToken) { duplicatePrimary.primary = span.clone(); }
  auto wrongDuplicatePrimaryResult = BindingVerifier::verify(input, zc::mv(wrongDuplicatePrimary));
  ZC_EXPECT(requireBinderInvariant(wrongDuplicatePrimaryResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongDuplicateNoteSource = buildCandidate();
  auto& duplicateNoteSource =
      wrongDuplicateNoteSource.sourceFailures[duplicateFailureIndex(wrongDuplicateNoteSource)];
  ZC_REQUIRE(duplicateNoteSource.notes.size() == 1);
  ZC_IF_SOME(span, secondDuplicateToken) { duplicateNoteSource.notes[0].source = span.clone(); }
  auto wrongDuplicateNoteSourceResult =
      BindingVerifier::verify(input, zc::mv(wrongDuplicateNoteSource));
  ZC_EXPECT(requireBinderInvariant(wrongDuplicateNoteSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongDuplicateDiagnostic = buildCandidate();
  auto& duplicateDiagnostic =
      wrongDuplicateDiagnostic.sourceFailures[duplicateFailureIndex(wrongDuplicateDiagnostic)];
  duplicateDiagnostic.diagnostic = BinderDiagnosticCode::UndefinedIdentifier;
  auto wrongDuplicateDiagnosticResult =
      BindingVerifier::verify(input, zc::mv(wrongDuplicateDiagnostic));
  ZC_EXPECT(requireBinderInvariant(wrongDuplicateDiagnosticResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongDuplicateNoteDiagnostic = buildCandidate();
  auto& duplicateNoteDiagnostic =
      wrongDuplicateNoteDiagnostic
          .sourceFailures[duplicateFailureIndex(wrongDuplicateNoteDiagnostic)];
  ZC_REQUIRE(duplicateNoteDiagnostic.notes.size() == 1);
  duplicateNoteDiagnostic.notes[0].diagnostic = BinderDiagnosticCode::UndefinedIdentifier;
  auto wrongDuplicateNoteDiagnosticResult =
      BindingVerifier::verify(input, zc::mv(wrongDuplicateNoteDiagnostic));
  ZC_EXPECT(requireBinderInvariant(wrongDuplicateNoteDiagnosticResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("ExplicitClosureCaptures.PartitionsNestedExplicitAndInferredClosures") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(value: i32) {\n"
      "  const outer = () => {\n"
      "    const explicit = fun() use [value] {\n"
      "      value;\n"
      "      const inner = () => value;\n"
      "    };\n"
      "  };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto explicitNodes = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto lambdaNodes = nodesOfKind(input.tree(), ast::SyntaxKind::LambdaExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(explicitNodes.size() == 1);
  ZC_REQUIRE(lambdaNodes.size() == 2);
  ZC_REQUIRE(captureItems.size() == 1);
  const auto explicitClosure = requireDefinitionAt(input, explicitNodes[0]);
  const auto outerClosure = requireDefinitionAt(input, lambdaNodes[0]);
  const auto innerClosure = requireDefinitionAt(input, lambdaNodes[1]);
  const auto valueDefinition =
      requireNamedFrozenDefinition(input, "value"_zc, identity::DefinitionKind::Parameter);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  ZC_REQUIRE(metadata.explicitClosureCaptures().size() == 1);
  ZC_REQUIRE(metadata.closureFreeVariables().size() == 2);
  const auto& explicitFact =
      requireExplicitClosureCapture(metadata.explicitClosureCaptures(), explicitClosure);
  ZC_REQUIRE(explicitFact.captures.size() == 1);
  ZC_EXPECT(explicitFact.captures[0].target == valueDefinition);
  const auto& outer = requireClosureFreeVariable(metadata.closureFreeVariables(), outerClosure);
  const auto& inner = requireClosureFreeVariable(metadata.closureFreeVariables(), innerClosure);
  ZC_REQUIRE(requireFreeVariable(outer, valueDefinition).referenceSites.size() == 3);
  ZC_REQUIRE(requireFreeVariable(inner, valueDefinition).referenceSites.size() == 1);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingVerifier.RejectsMalformedExplicitClosureCaptureFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(first: i32, second: i32) {\n"
      "  const one = fun() use [first, &second] { first; second; };\n"
      "  const two = fun() use [] {};\n"
      "  const inferred = () => first;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  const auto captureItems = nodesOfKind(input.tree(), ast::SyntaxKind::CaptureItem);
  ZC_REQUIRE(closures.size() == 2);
  ZC_REQUIRE(captureItems.size() == 2);
  const auto one = requireDefinitionAt(input, closures[0]);
  const auto run =
      requireNamedFrozenDefinition(input, "run"_zc, identity::DefinitionKind::Function);
  const auto second =
      requireNamedFrozenDefinition(input, "second"_zc, identity::DefinitionKind::Parameter);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("explicit capture mutation fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };

  auto missingRow = buildCandidate();
  ZC_REQUIRE(missingRow.explicitClosureCaptures.size() == 2);
  missingRow.explicitClosureCaptures.removeLast();
  auto missingRowResult = BindingVerifier::verify(input, zc::mv(missingRow));
  ZC_EXPECT(requireBinderInvariant(missingRowResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalRow = buildCandidate();
  additionalRow.explicitClosureCaptures.add(
      cloneExplicitClosureCapture(additionalRow.explicitClosureCaptures[0]));
  auto additionalRowResult = BindingVerifier::verify(input, zc::mv(additionalRow));
  ZC_EXPECT(requireBinderInvariant(additionalRowResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedRows = buildCandidate();
  auto displacedRow = zc::mv(reorderedRows.explicitClosureCaptures[0]);
  reorderedRows.explicitClosureCaptures[0] = zc::mv(reorderedRows.explicitClosureCaptures[1]);
  reorderedRows.explicitClosureCaptures[1] = zc::mv(displacedRow);
  auto reorderedRowsResult = BindingVerifier::verify(input, zc::mv(reorderedRows));
  ZC_EXPECT(requireBinderInvariant(reorderedRowsResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongClosure = buildCandidate();
  wrongClosure.explicitClosureCaptures[0].closure = run;
  auto wrongClosureResult = BindingVerifier::verify(input, zc::mv(wrongClosure));
  ZC_EXPECT(requireBinderInvariant(wrongClosureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongList = buildCandidate();
  wrongList.explicitClosureCaptures[0].captureList =
      wrongList.explicitClosureCaptures[1].captureList;
  auto wrongListResult = BindingVerifier::verify(input, zc::mv(wrongList));
  ZC_EXPECT(requireBinderInvariant(wrongListResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongRowSource = buildCandidate();
  wrongRowSource.explicitClosureCaptures[0].source =
      wrongRowSource.explicitClosureCaptures[1].source.clone();
  auto wrongRowSourceResult = BindingVerifier::verify(input, zc::mv(wrongRowSource));
  ZC_EXPECT(requireBinderInvariant(wrongRowSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingCapture = buildCandidate();
  auto& missingCaptureRow =
      requireExplicitClosureCapture(missingCapture.explicitClosureCaptures.asPtr(), one);
  ZC_REQUIRE(missingCaptureRow.captures.size() == 2);
  missingCaptureRow.captures.removeLast();
  auto missingCaptureResult = BindingVerifier::verify(input, zc::mv(missingCapture));
  ZC_EXPECT(requireBinderInvariant(missingCaptureResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalCapture = buildCandidate();
  auto& additionalCaptureRow =
      requireExplicitClosureCapture(additionalCapture.explicitClosureCaptures.asPtr(), one);
  additionalCaptureRow.captures.add(cloneExplicitCapture(additionalCaptureRow.captures[0]));
  auto additionalCaptureResult = BindingVerifier::verify(input, zc::mv(additionalCapture));
  ZC_EXPECT(requireBinderInvariant(additionalCaptureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedCaptures = buildCandidate();
  auto& reorderedCaptureRow =
      requireExplicitClosureCapture(reorderedCaptures.explicitClosureCaptures.asPtr(), one);
  auto displacedCapture = zc::mv(reorderedCaptureRow.captures[0]);
  reorderedCaptureRow.captures[0] = zc::mv(reorderedCaptureRow.captures[1]);
  reorderedCaptureRow.captures[1] = zc::mv(displacedCapture);
  auto reorderedCaptureResult = BindingVerifier::verify(input, zc::mv(reorderedCaptures));
  ZC_EXPECT(requireBinderInvariant(reorderedCaptureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongTarget = buildCandidate();
  requireExplicitClosureCapture(wrongTarget.explicitClosureCaptures.asPtr(), one)
      .captures[0]
      .target = second;
  auto wrongTargetResult = BindingVerifier::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(wrongTargetResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongItem = buildCandidate();
  requireExplicitClosureCapture(wrongItem.explicitClosureCaptures.asPtr(), one).captures[0].item =
      captureItems[1];
  auto wrongItemResult = BindingVerifier::verify(input, zc::mv(wrongItem));
  ZC_EXPECT(requireBinderInvariant(wrongItemResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongSource = buildCandidate();
  auto& wrongSourceRow =
      requireExplicitClosureCapture(wrongSource.explicitClosureCaptures.asPtr(), one);
  wrongSourceRow.captures[0].source = wrongSourceRow.source.clone();
  auto wrongSourceResult = BindingVerifier::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(wrongSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongResolution = buildCandidate();
  auto& bound = requireResolution(wrongResolution.nodeBindings.asPtr(), captureItems[0])
                    .value.get<BoundNameResolution>();
  bound.origin = BindingOrigin::ImportAlias;
  auto wrongResolutionResult = BindingVerifier::verify(input, zc::mv(wrongResolution));
  ZC_EXPECT(requireBinderInvariant(wrongResolutionResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingResolution = buildCandidate();
  size_t bindingIndex = missingResolution.nodeBindings.size();
  for (size_t index = 0; index < missingResolution.nodeBindings.size(); ++index) {
    if (missingResolution.nodeBindings[index].node == captureItems[0]) {
      bindingIndex = index;
      break;
    }
  }
  ZC_REQUIRE(bindingIndex < missingResolution.nodeBindings.size());
  for (size_t index = bindingIndex; index + 1 < missingResolution.nodeBindings.size(); ++index) {
    missingResolution.nodeBindings[index] = zc::mv(missingResolution.nodeBindings[index + 1]);
  }
  missingResolution.nodeBindings.removeLast();
  auto missingResolutionResult = BindingVerifier::verify(input, zc::mv(missingResolution));
  ZC_EXPECT(requireBinderInvariant(missingResolutionResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto overlappingPartition = buildCandidate();
  zc::Vector<FreeVariableFact> noVariables;
  overlappingPartition.closureFreeVariables.add(ClosureFreeVariableFact{one, zc::mv(noVariables)});
  auto overlappingPartitionResult = BindingVerifier::verify(input, zc::mv(overlappingPartition));
  ZC_EXPECT(requireBinderInvariant(overlappingPartitionResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingVerifier.RejectsForeignExplicitCaptureIdentities") {
  ParsedSource localSource(
      "module root;\n"
      "fun run(value: i32) { const closure = fun() use [value] {}; }\n"_zc);
  ParsedSource foreignSource(
      "module root;\n"
      "fun run(value: i32) { const closure = fun() use [value] {}; }\n"_zc);
  FrozenFixture localFixture(localSource, true);
  FrozenFixture foreignFixture(foreignSource, true);
  auto inputResult = verify(localFixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto foreignInputResult = verify(foreignFixture);
  ZC_REQUIRE(foreignInputResult.is<VerifiedBindingInput>());
  auto foreignInput = zc::mv(foreignInputResult.get<VerifiedBindingInput>());
  auto localCandidate = BindingBuilder::build(input, *localSource.diagnostics);
  auto foreignCandidate = BindingBuilder::build(foreignInput, *foreignSource.diagnostics);
  ZC_REQUIRE(localCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(foreignCandidate.is<BindingMetadataCandidate>());
  auto& local = localCandidate.get<BindingMetadataCandidate>();
  auto& foreign = foreignCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(local.explicitClosureCaptures.size() == 1);
  ZC_REQUIRE(foreign.explicitClosureCaptures.size() == 1);
  ZC_REQUIRE(local.explicitClosureCaptures[0].captures.size() == 1);
  ZC_REQUIRE(foreign.explicitClosureCaptures[0].captures.size() == 1);
  const auto foreignClosure = foreign.explicitClosureCaptures[0].closure;
  const auto foreignTarget = foreign.explicitClosureCaptures[0].captures[0].target;

  local.explicitClosureCaptures[0].closure = foreignClosure;
  auto closureRejected = BindingVerifier::verify(input, zc::mv(local));
  ZC_EXPECT(requireIdentityInvariant(closureRejected).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto targetCandidate = BindingBuilder::build(input, *localSource.diagnostics);
  ZC_REQUIRE(targetCandidate.is<BindingMetadataCandidate>());
  auto& target = targetCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(target.explicitClosureCaptures.size() == 1);
  ZC_REQUIRE(target.explicitClosureCaptures[0].captures.size() == 1);
  target.explicitClosureCaptures[0].captures[0].target = foreignTarget;
  auto targetRejected = BindingVerifier::verify(input, zc::mv(target));
  ZC_EXPECT(requireIdentityInvariant(targetRejected).kind() ==
            identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("ClosureFreeVariables.RejectsCrossFunctionCapture") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun barrier() {}\n"
      "fun outer(value: i32) { const closure = () => value; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto references = identifierExpressions(input.tree(), "value"_zc);
  const auto closures = nodesOfKind(input.tree(), ast::SyntaxKind::LambdaExpression);
  const auto barrier =
      requireNamedFrozenDefinition(input, "barrier"_zc, identity::DefinitionKind::Function);
  ZC_REQUIRE(references.size() == 1);
  ZC_REQUIRE(closures.size() == 1);
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  auto arenaResult = ScopeArenaBuilder::build(input);
  ZC_REQUIRE(arenaResult.is<ScopeArenaCandidate>());
  auto arena = zc::mv(arenaResult.get<ScopeArenaCandidate>());
  zc::Maybe<uint32_t> closureScopeIndex;
  for (const auto& nodeScope : arena.nodeScopes) {
    if (nodeScope.node == closures[0]) { closureScopeIndex = nodeScope.scope.index(); }
  }
  ZC_REQUIRE(closureScopeIndex != zc::none);
  const auto closureScope = ZC_ASSERT_NONNULL(closureScopeIndex);
  ZC_REQUIRE(closureScope < arena.scopes.size());
  ZC_REQUIRE(arena.scopes[closureScope].parent != zc::none);
  const auto interveningScope = ZC_ASSERT_NONNULL(arena.scopes[closureScope].parent).index();
  ZC_REQUIRE(interveningScope < arena.scopes.size());
  arena.scopes[interveningScope].kind = ScopeKind::Function;
  arena.scopes[interveningScope].owner = ScopeOwner::definition(barrier);
  auto closureResult = ClosureFreeVariableBuilder::build(input, arena, value.definitions.asPtr(),
                                                         value.nodeBindings.asPtr());
  ZC_REQUIRE(closureResult.is<BinderInvariantFact>());
  const auto& fact = closureResult.get<BinderInvariantFact>();
  ZC_EXPECT(fact.kind == BinderInvariantKind::MalformedScopeGraph);
  ZC_EXPECT(fact.emitterSite == BinderEmitterSite::LabelAndClosure);
  ZC_EXPECT(fact.schemaPreorderOrdinal == schemaPreorderOrdinal(input.tree(), references[0]));
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingVerifier.RejectsMalformedClosureFreeVariableFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(first: i32, second: i32) {\n"
      "  const outer = () => {\n"
      "    first; second; first;\n"
      "    const inner = () => first;\n"
      "  };\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto lambdas = nodesOfKind(input.tree(), ast::SyntaxKind::LambdaExpression);
  ZC_REQUIRE(lambdas.size() == 2);
  const auto outer = requireDefinitionAt(input, lambdas[0]);
  const auto first =
      requireNamedFrozenDefinition(input, "first"_zc, identity::DefinitionKind::Parameter);
  const auto run =
      requireNamedFrozenDefinition(input, "run"_zc, identity::DefinitionKind::Function);
  const auto secondSites = identifierExpressions(input.tree(), "second"_zc);
  ZC_REQUIRE(secondSites.size() == 1);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("closure mutation fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };

  auto missingClosure = buildCandidate();
  ZC_REQUIRE(missingClosure.closureFreeVariables.size() == 2);
  missingClosure.closureFreeVariables.removeLast();
  auto missingClosureResult = BindingVerifier::verify(input, zc::mv(missingClosure));
  ZC_EXPECT(requireBinderInvariant(missingClosureResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalClosure = buildCandidate();
  auto duplicateClosure = cloneClosureFreeVariable(additionalClosure.closureFreeVariables[0]);
  additionalClosure.closureFreeVariables.add(zc::mv(duplicateClosure));
  auto additionalClosureResult = BindingVerifier::verify(input, zc::mv(additionalClosure));
  ZC_EXPECT(requireBinderInvariant(additionalClosureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedClosures = buildCandidate();
  auto displacedClosure = zc::mv(reorderedClosures.closureFreeVariables[0]);
  reorderedClosures.closureFreeVariables[0] = zc::mv(reorderedClosures.closureFreeVariables[1]);
  reorderedClosures.closureFreeVariables[1] = zc::mv(displacedClosure);
  auto reorderedClosureResult = BindingVerifier::verify(input, zc::mv(reorderedClosures));
  ZC_EXPECT(requireBinderInvariant(reorderedClosureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongClosure = buildCandidate();
  wrongClosure.closureFreeVariables[0].closure = run;
  auto wrongClosureResult = BindingVerifier::verify(input, zc::mv(wrongClosure));
  ZC_EXPECT(requireBinderInvariant(wrongClosureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingVariable = buildCandidate();
  auto& missingVariableClosure =
      requireClosureFreeVariable(missingVariable.closureFreeVariables.asPtr(), outer);
  ZC_REQUIRE(missingVariableClosure.variables.size() == 2);
  missingVariableClosure.variables.removeLast();
  auto missingVariableResult = BindingVerifier::verify(input, zc::mv(missingVariable));
  ZC_EXPECT(requireBinderInvariant(missingVariableResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalVariable = buildCandidate();
  auto& additionalVariableClosure =
      requireClosureFreeVariable(additionalVariable.closureFreeVariables.asPtr(), outer);
  auto duplicateVariable = cloneFreeVariable(additionalVariableClosure.variables[0]);
  additionalVariableClosure.variables.add(zc::mv(duplicateVariable));
  auto additionalVariableResult = BindingVerifier::verify(input, zc::mv(additionalVariable));
  ZC_EXPECT(requireBinderInvariant(additionalVariableResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedVariables = buildCandidate();
  auto& reorderedVariableClosure =
      requireClosureFreeVariable(reorderedVariables.closureFreeVariables.asPtr(), outer);
  auto displacedVariable = zc::mv(reorderedVariableClosure.variables[0]);
  reorderedVariableClosure.variables[0] = zc::mv(reorderedVariableClosure.variables[1]);
  reorderedVariableClosure.variables[1] = zc::mv(displacedVariable);
  auto reorderedVariableResult = BindingVerifier::verify(input, zc::mv(reorderedVariables));
  ZC_EXPECT(requireBinderInvariant(reorderedVariableResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongTarget = buildCandidate();
  auto& wrongTargetClosure =
      requireClosureFreeVariable(wrongTarget.closureFreeVariables.asPtr(), outer);
  wrongTargetClosure.variables[0].target = run;
  auto wrongTargetResult = BindingVerifier::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(wrongTargetResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingSite = buildCandidate();
  auto& missingSiteFact = requireFreeVariable(
      requireClosureFreeVariable(missingSite.closureFreeVariables.asPtr(), outer), first);
  ZC_REQUIRE(missingSiteFact.referenceSites.size() >= 2);
  missingSiteFact.referenceSites.removeLast();
  auto missingSiteResult = BindingVerifier::verify(input, zc::mv(missingSite));
  ZC_EXPECT(requireBinderInvariant(missingSiteResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalSite = buildCandidate();
  auto& additionalSiteFact = requireFreeVariable(
      requireClosureFreeVariable(additionalSite.closureFreeVariables.asPtr(), outer), first);
  additionalSiteFact.referenceSites.add(additionalSiteFact.referenceSites[0]);
  auto additionalSiteResult = BindingVerifier::verify(input, zc::mv(additionalSite));
  ZC_EXPECT(requireBinderInvariant(additionalSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedSites = buildCandidate();
  auto& reorderedSiteFact = requireFreeVariable(
      requireClosureFreeVariable(reorderedSites.closureFreeVariables.asPtr(), outer), first);
  const auto displacedSite = reorderedSiteFact.referenceSites[0];
  reorderedSiteFact.referenceSites[0] = reorderedSiteFact.referenceSites[1];
  reorderedSiteFact.referenceSites[1] = displacedSite;
  auto reorderedSiteResult = BindingVerifier::verify(input, zc::mv(reorderedSites));
  ZC_EXPECT(requireBinderInvariant(reorderedSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongSite = buildCandidate();
  auto& wrongSiteFact = requireFreeVariable(
      requireClosureFreeVariable(wrongSite.closureFreeVariables.asPtr(), outer), first);
  wrongSiteFact.referenceSites[0] = secondSites[0];
  auto wrongSiteResult = BindingVerifier::verify(input, zc::mv(wrongSite));
  ZC_EXPECT(requireBinderInvariant(wrongSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto outsideSite = buildCandidate();
  auto& outsideSiteFact = requireFreeVariable(
      requireClosureFreeVariable(outsideSite.closureFreeVariables.asPtr(), outer), first);
  outsideSiteFact.referenceSites[0] = ast::NodeId(input.tree().nodeCount() + 1);
  auto outsideSiteResult = BindingVerifier::verify(input, zc::mv(outsideSite));
  ZC_EXPECT(requireBinderInvariant(outsideSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsForeignClosureFreeVariableIdentities") {
  const auto source = "module root; fun run(value: i32) { const closure = () => value; }"_zc;
  ParsedSource localSource(source);
  ParsedSource foreignSource(source);
  FrozenFixture localFixture(localSource, true);
  FrozenFixture foreignFixture(foreignSource, true);
  auto localInputResult = verify(localFixture);
  auto foreignInputResult = verify(foreignFixture);
  ZC_REQUIRE(localInputResult.is<VerifiedBindingInput>());
  ZC_REQUIRE(foreignInputResult.is<VerifiedBindingInput>());
  auto localInput = zc::mv(localInputResult.get<VerifiedBindingInput>());
  auto foreignInput = zc::mv(foreignInputResult.get<VerifiedBindingInput>());
  const auto localClosures = nodesOfKind(localInput.tree(), ast::SyntaxKind::LambdaExpression);
  const auto foreignClosures = nodesOfKind(foreignInput.tree(), ast::SyntaxKind::LambdaExpression);
  ZC_REQUIRE(localClosures.size() == 1);
  ZC_REQUIRE(foreignClosures.size() == 1);
  const auto localClosure = requireDefinitionAt(localInput, localClosures[0]);
  const auto foreignClosure = requireDefinitionAt(foreignInput, foreignClosures[0]);
  const auto foreignTarget =
      requireNamedFrozenDefinition(foreignInput, "value"_zc, identity::DefinitionKind::Parameter);

  auto closureCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(closureCandidate.is<BindingMetadataCandidate>());
  auto& foreignClosureFact = closureCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(foreignClosureFact.closureFreeVariables.size() == 1);
  foreignClosureFact.closureFreeVariables[0].closure = foreignClosure;
  auto closureResult = BindingVerifier::verify(localInput, zc::mv(foreignClosureFact));
  ZC_EXPECT(requireIdentityInvariant(closureResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto targetCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(targetCandidate.is<BindingMetadataCandidate>());
  auto& foreignTargetFact = targetCandidate.get<BindingMetadataCandidate>();
  auto& closure =
      requireClosureFreeVariable(foreignTargetFact.closureFreeVariables.asPtr(), localClosure);
  ZC_REQUIRE(closure.variables.size() == 1);
  closure.variables[0].target = foreignTarget;
  auto targetResult = BindingVerifier::verify(localInput, zc::mv(foreignTargetFact));
  ZC_EXPECT(requireIdentityInvariant(targetResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);
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

ZC_TEST("BodyBinding.ResolvesModuleOwnedLoopPatternInBody") {
  ParsedSource sourceFixture(
      "module root;\n"
      "for (let item in [1]) { item; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto item =
      requireNamedFrozenDefinition(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  const auto references = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(references.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireDefinitionFact(metadata.definitions(), item);
  ZC_EXPECT(fact.kind == identity::DefinitionKind::PatternBinding);
  ZC_EXPECT(fact.activation == DefinitionActivation::LoopPattern);
  ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Loop);
  const auto& owner = metadata.scopes()[fact.declaringScope.index()].owner.value();
  ZC_REQUIRE(owner.is<ModuleScopeOwner>());
  ZC_EXPECT(owner.get<ModuleScopeOwner>().module == input.module());
  const auto& resolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  const auto& bound = resolution.value.get<BoundNameResolution>();
  ZC_EXPECT(requireDefinitionTarget(bound.bindingIdentity) == item);
  ZC_EXPECT(requireDefinitionTarget(bound.canonicalTarget) == item);
  ZC_EXPECT(bound.nameSpace == Namespace::Value);
  ZC_EXPECT(bound.origin == BindingOrigin::LocalDeclaration);
  auto token =
      input.parsedModule().retainedTokenSpan(references[0], 0, ast::SyntaxKind::Identifier);
  auto source = input.parsedModule().spanFor(input.tree().node(references[0]).range);
  ZC_REQUIRE(token != zc::none);
  ZC_REQUIRE(source != zc::none);
  ZC_IF_SOME(tokenSpan, token) {
    ZC_IF_SOME(sourceSpan, source) { ZC_EXPECT(sameSpan(tokenSpan, sourceSpan)); }
  }
  ZC_EXPECT(metadata.closureFreeVariables().size() == 0);
  ZC_EXPECT(metadata.explicitClosureCaptures().size() == 0);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BodyBinding.ResolvesModuleOwnedMatchPatternInGuardAndBody") {
  ParsedSource sourceFixture(
      "module root;\n"
      "match (1) {\n"
      "  when matched if matched > 0 => { matched; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto matched =
      requireNamedFrozenDefinition(input, "matched"_zc, identity::DefinitionKind::PatternBinding);
  const auto references = identifierExpressions(input.tree(), "matched"_zc);
  ZC_REQUIRE(references.size() == 2);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireDefinitionFact(metadata.definitions(), matched);
  ZC_EXPECT(fact.kind == identity::DefinitionKind::PatternBinding);
  ZC_EXPECT(fact.activation == DefinitionActivation::MatchPattern);
  ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::MatchArm);
  const auto& owner = metadata.scopes()[fact.declaringScope.index()].owner.value();
  ZC_REQUIRE(owner.is<ModuleScopeOwner>());
  ZC_EXPECT(owner.get<ModuleScopeOwner>().module == input.module());
  for (const auto reference : references) {
    const auto& resolution = requireResolution(metadata.nodeBindings(), reference);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    const auto& bound = resolution.value.get<BoundNameResolution>();
    ZC_EXPECT(requireDefinitionTarget(bound.bindingIdentity) == matched);
    ZC_EXPECT(requireDefinitionTarget(bound.canonicalTarget) == matched);
    ZC_EXPECT(bound.nameSpace == Namespace::Value);
    ZC_EXPECT(bound.origin == BindingOrigin::LocalDeclaration);
    auto token = input.parsedModule().retainedTokenSpan(reference, 0, ast::SyntaxKind::Identifier);
    auto source = input.parsedModule().spanFor(input.tree().node(reference).range);
    ZC_REQUIRE(token != zc::none);
    ZC_REQUIRE(source != zc::none);
    ZC_IF_SOME(tokenSpan, token) {
      ZC_IF_SOME(sourceSpan, source) { ZC_EXPECT(sameSpan(tokenSpan, sourceSpan)); }
    }
  }
  ZC_EXPECT(metadata.closureFreeVariables().size() == 0);
  ZC_EXPECT(metadata.explicitClosureCaptures().size() == 0);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BodyBinding.ResolvesModuleOwnedLocalThroughNestedBlock") {
  ParsedSource sourceFixture(
      "module root;\n"
      "for (let item in [1]) {\n"
      "  let local = 1;\n"
      "  { local; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto local =
      requireNamedFrozenDefinition(input, "local"_zc, identity::DefinitionKind::Local);
  const auto references = identifierExpressions(input.tree(), "local"_zc);
  ZC_REQUIRE(references.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireDefinitionFact(metadata.definitions(), local);
  ZC_EXPECT(fact.kind == identity::DefinitionKind::Local);
  ZC_EXPECT(fact.activation == DefinitionActivation::AfterInitializer);
  ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Block);
  const auto& owner = metadata.scopes()[fact.declaringScope.index()].owner.value();
  ZC_REQUIRE(owner.is<ModuleScopeOwner>());
  ZC_EXPECT(owner.get<ModuleScopeOwner>().module == input.module());
  const auto& resolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  const auto& bound = resolution.value.get<BoundNameResolution>();
  ZC_EXPECT(requireDefinitionTarget(bound.bindingIdentity) == local);
  ZC_EXPECT(requireDefinitionTarget(bound.canonicalTarget) == local);
  ZC_EXPECT(bound.nameSpace == Namespace::Value);
  ZC_EXPECT(bound.origin == BindingOrigin::LocalDeclaration);
  auto token =
      input.parsedModule().retainedTokenSpan(references[0], 0, ast::SyntaxKind::Identifier);
  auto source = input.parsedModule().spanFor(input.tree().node(references[0]).range);
  ZC_REQUIRE(token != zc::none);
  ZC_REQUIRE(source != zc::none);
  ZC_IF_SOME(tokenSpan, token) {
    ZC_IF_SOME(sourceSpan, source) { ZC_EXPECT(sameSpan(tokenSpan, sourceSpan)); }
  }
  ZC_EXPECT(metadata.closureFreeVariables().size() == 0);
  ZC_EXPECT(metadata.explicitClosureCaptures().size() == 0);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
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

ZC_TEST("DeferredMember.PublishesCanonicalFactsAndGenericArguments") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Type {}\n"
      "fun run(value: Type) {\n"
      "  value.field;\n"
      "  value.method<Type>();\n"
      "  value.inner.leaf;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_EXPECT(value.sourceFailures.empty());
  ZC_REQUIRE(value.deferredMembers.size() == 4);

  const auto fields = memberExpressions(input.tree(), "field"_zc);
  const auto methods = memberExpressions(input.tree(), "method"_zc);
  const auto inners = memberExpressions(input.tree(), "inner"_zc);
  const auto leaves = memberExpressions(input.tree(), "leaf"_zc);
  ZC_REQUIRE(fields.size() == 1);
  ZC_REQUIRE(methods.size() == 1);
  ZC_REQUIRE(inners.size() == 1);
  ZC_REQUIRE(leaves.size() == 1);

  for (const auto node : {fields[0], methods[0], inners[0], leaves[0]}) {
    const auto& fact = requireDeferredMember(value.deferredMembers.asPtr(), node);
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), node);
    ZC_REQUIRE(resolution.value.is<DeferredMemberFact>());
    const auto& resolved = resolution.value.get<DeferredMemberFact>();
    ZC_EXPECT(fact.node == node);
    ZC_EXPECT(fact.base ==
              ast::NodeId(input.tree().node(node).payload.words[ast::kMemberExpressionObjectWord]));
    ZC_REQUIRE(fact.expectedNamespaces.size() == 1);
    ZC_EXPECT(fact.expectedNamespaces[0] == Namespace::Value);
    ZC_EXPECT(resolved.node == fact.node);
    ZC_EXPECT(resolved.base == fact.base);
    ZC_EXPECT(resolved.member == fact.member);
    ZC_EXPECT(sameSpan(resolved.source, fact.source));
    auto nodeSource = input.parsedModule().spanFor(input.tree().node(node).range);
    ZC_REQUIRE(nodeSource != zc::none);
    ZC_IF_SOME(source, nodeSource) { ZC_EXPECT(sameSpan(fact.source, source)); }
  }

  const auto& field = requireDeferredMember(value.deferredMembers.asPtr(), fields[0]);
  ZC_EXPECT(field.member.text() == "field"_zc);
  ZC_EXPECT(field.genericArguments.empty());

  const auto& method = requireDeferredMember(value.deferredMembers.asPtr(), methods[0]);
  ZC_EXPECT(method.member.text() == "method"_zc);
  ZC_REQUIRE(method.genericArguments.size() == 1);
  ZC_EXPECT(input.tree().node(method.genericArguments[0]).kind == ast::SyntaxKind::NamedTypeExpr);

  const auto& leaf = requireDeferredMember(value.deferredMembers.asPtr(), leaves[0]);
  ZC_EXPECT(leaf.base == inners[0]);
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  ZC_EXPECT(verified.get<VerifiedBindingOutput>().metadata.deferredMembers().size() == 4);
}

ZC_TEST("DeferredMember.PublishesSpecialDeclaredMemberName") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Type {}\n"
      "fun run(value: Type) { value.init; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  const auto members = memberExpressions(input.tree(), "init"_zc);
  ZC_REQUIRE(members.size() == 1);
  const auto& fact = requireDeferredMember(value.deferredMembers.asPtr(), members[0]);
  ZC_EXPECT(fact.member.text() == "init"_zc);
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
}

ZC_TEST("DeferredMember.PublishesOptionalMember") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Type {}\n"
      "fun run(value: Type) { value?.field; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  const auto members = memberExpressions(input.tree(), "field"_zc);
  ZC_REQUIRE(members.size() == 1);
  const auto& fact = requireDeferredMember(value.deferredMembers.asPtr(), members[0]);
  auto source = input.parsedModule().spanFor(input.tree().node(members[0]).range);
  ZC_REQUIRE(source != zc::none);
  ZC_IF_SOME(expected, source) { ZC_EXPECT(sameSpan(fact.source, expected)); }
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
}

ZC_TEST("DeferredMember.RejectsQualifiedAccessWithoutVerifiedContext") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Type {}\n"
      "fun run(value: Type) { value::field; }\n"_zc);
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

ZC_TEST("BindingVerifier.RejectsMalformedDeferredMemberFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Type {}\n"
      "fun run(value: Type) { value.method<Type>(); value.field; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto methods = memberExpressions(input.tree(), "method"_zc);
  const auto fields = memberExpressions(input.tree(), "field"_zc);
  ZC_REQUIRE(methods.size() == 1);
  ZC_REQUIRE(fields.size() == 1);

  auto baseCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(baseCandidate.is<BindingMetadataCandidate>());
  auto& wrongBase = baseCandidate.get<BindingMetadataCandidate>();
  auto& baseFact = requireDeferredMember(wrongBase.deferredMembers.asPtr(), methods[0]);
  auto& baseResolution = requireResolution(wrongBase.nodeBindings.asPtr(), methods[0]);
  ZC_REQUIRE(baseResolution.value.is<DeferredMemberFact>());
  baseFact.base = methods[0];
  baseResolution.value.get<DeferredMemberFact>().base = methods[0];
  auto baseRejected = BindingVerifier::verify(input, zc::mv(wrongBase));
  ZC_EXPECT(requireBinderInvariant(baseRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto namespaceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(namespaceCandidate.is<BindingMetadataCandidate>());
  auto& wrongNamespace = namespaceCandidate.get<BindingMetadataCandidate>();
  auto& namespaceFact = requireDeferredMember(wrongNamespace.deferredMembers.asPtr(), methods[0]);
  auto& namespaceResolution = requireResolution(wrongNamespace.nodeBindings.asPtr(), methods[0]);
  ZC_REQUIRE(namespaceResolution.value.is<DeferredMemberFact>());
  namespaceFact.expectedNamespaces[0] = Namespace::Type;
  namespaceResolution.value.get<DeferredMemberFact>().expectedNamespaces[0] = Namespace::Type;
  auto namespaceRejected = BindingVerifier::verify(input, zc::mv(wrongNamespace));
  ZC_EXPECT(requireBinderInvariant(namespaceRejected).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto genericCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(genericCandidate.is<BindingMetadataCandidate>());
  auto& wrongGeneric = genericCandidate.get<BindingMetadataCandidate>();
  auto& genericFact = requireDeferredMember(wrongGeneric.deferredMembers.asPtr(), methods[0]);
  auto& genericResolution = requireResolution(wrongGeneric.nodeBindings.asPtr(), methods[0]);
  ZC_REQUIRE(genericResolution.value.is<DeferredMemberFact>());
  genericFact.genericArguments.clear();
  genericResolution.value.get<DeferredMemberFact>().genericArguments.clear();
  auto genericRejected = BindingVerifier::verify(input, zc::mv(wrongGeneric));
  ZC_EXPECT(requireBinderInvariant(genericRejected).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto sourceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(sourceCandidate.is<BindingMetadataCandidate>());
  auto& wrongSource = sourceCandidate.get<BindingMetadataCandidate>();
  auto& sourceFact = requireDeferredMember(wrongSource.deferredMembers.asPtr(), methods[0]);
  auto& sourceResolution = requireResolution(wrongSource.nodeBindings.asPtr(), methods[0]);
  ZC_REQUIRE(sourceResolution.value.is<DeferredMemberFact>());
  const auto& replacementSource =
      requireDeferredMember(wrongSource.deferredMembers.asPtr(), fields[0]).source;
  sourceFact.source = replacementSource.clone();
  sourceResolution.value.get<DeferredMemberFact>().source = replacementSource.clone();
  auto sourceRejected = BindingVerifier::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(sourceRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto orderCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(orderCandidate.is<BindingMetadataCandidate>());
  auto& wrongOrder = orderCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(wrongOrder.deferredMembers.size() == 2);
  auto firstFact = zc::mv(wrongOrder.deferredMembers[0]);
  wrongOrder.deferredMembers[0] = zc::mv(wrongOrder.deferredMembers[1]);
  wrongOrder.deferredMembers[1] = zc::mv(firstFact);
  auto orderRejected = BindingVerifier::verify(input, zc::mv(wrongOrder));
  ZC_EXPECT(requireBinderInvariant(orderRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  missing.deferredMembers.removeLast();
  auto missingRejected = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingRejected).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto resolutionCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(resolutionCandidate.is<BindingMetadataCandidate>());
  auto& wrongResolution = resolutionCandidate.get<BindingMetadataCandidate>();
  auto& resolution = requireResolution(wrongResolution.nodeBindings.asPtr(), fields[0]);
  ZC_REQUIRE(resolution.value.is<DeferredMemberFact>());
  auto changedName = identity::DeclaredDefinitionName::fromCanonical("changed"_zc);
  ZC_REQUIRE(changedName != zc::none);
  ZC_IF_SOME(name, changedName) {
    requireDeferredMember(wrongResolution.deferredMembers.asPtr(), fields[0]).member = name.clone();
    resolution.value.get<DeferredMemberFact>().member = zc::mv(name);
  }
  auto resolutionRejected = BindingVerifier::verify(input, zc::mv(wrongResolution));
  ZC_EXPECT(requireBinderInvariant(resolutionRejected).kind ==
            BinderInvariantKind::InvalidBindingFact);
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
