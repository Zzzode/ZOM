// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-input.h"

#include "parsed-module-query-test-fixture.h"
#include "zc/core/encoding.h"
#include "zc/core/io.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/binding-input-diagnostic-adapter.h"
#include "zomlang/compiler/binder/binding-run.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/internal/binding-skeleton.h"
#include "zomlang/compiler/binder/internal/binding-verifier.h"
#include "zomlang/compiler/binder/internal/closure-free-variables.h"
#include "zomlang/compiler/binder/internal/label-facts.h"
#include "zomlang/compiler/binder/internal/scope-arena.h"
#include "zomlang/compiler/binder/internal/verified-module-graph-storage.h"
#include "zomlang/compiler/binder/module-dependency-requests.h"
#include "zomlang/compiler/binder/module-graph-diagnostic-adapter.h"
#include "zomlang/compiler/binder/module-resolution.h"
#include "zomlang/compiler/binder/stable-identity-candidate-producer.h"
#include "zomlang/compiler/binder/stable-identity-candidate-verifier.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/diagnostics/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {

namespace test {

class VerifiedModuleGraphFixture final {
public:
  static VerifiedModuleGraph build(identity::SemanticContextBrand context,
                                   const identity::SemanticContextFingerprint& fingerprint,
                                   zc::ArrayPtr<const ModuleGraphModule> modules,
                                   zc::ArrayPtr<const identity::SourceFileKey> sources,
                                   zc::ArrayPtr<const ModuleDependencyRequest> requests = {},
                                   zc::ArrayPtr<const identity::ModuleId> targets = {}) {
    ZC_REQUIRE(modules.size() != 0);
    ZC_REQUIRE(modules.size() == sources.size());
    ZC_REQUIRE(requests.size() == targets.size());

    zc::Vector<identity::ModuleKey> keys(modules.size());
    zc::Vector<identity::ModuleId> handles(modules.size());
    zc::Vector<identity::SourceFileKey> selectedSources(sources.size());
    for (size_t index = 0; index < modules.size(); ++index) {
      keys.add(modules[index].key().clone());
      handles.add(modules[index].module());
      selectedSources.add(sources[index].clone());
    }

    zc::Vector<VerifiedModuleDependencyEdge> edges(requests.size());
    for (size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex) {
      zc::Maybe<const identity::ModuleKey&> requester;
      zc::Maybe<const identity::ModuleKey&> target;
      for (size_t moduleIndex = 0; moduleIndex < modules.size(); ++moduleIndex) {
        if (modules[moduleIndex].module() == requests[requestIndex].requester()) {
          requester = modules[moduleIndex].key();
        }
        if (modules[moduleIndex].module() == targets[requestIndex]) {
          target = modules[moduleIndex].key();
        }
      }
      ZC_REQUIRE(requester != zc::none);
      ZC_REQUIRE(target != zc::none);
      identity::CanonicalEncoder encoder;
      ZC_ASSERT_NONNULL(requester).encode(encoder);
      encoder.encodeByteString(requests[requestIndex].key().encode().asPtr());
      ZC_ASSERT_NONNULL(target).encode(encoder);
      edges.add(VerifiedModuleDependencyEdge(requests[requestIndex].clone(), targets[requestIndex],
                                             encoder.finish()));
    }
    for (size_t index = 1; index < edges.size(); ++index) {
      auto current = zc::mv(edges[index]);
      size_t insertion = index;
      while (insertion != 0 && current.encodedKey() < edges[insertion - 1].encodedKey()) {
        edges[insertion] = zc::mv(edges[insertion - 1]);
        --insertion;
      }
      edges[insertion] = zc::mv(current);
    }

    identity::CanonicalEncoder revisionBytes;
    revisionBytes.encodeDigest(fingerprint.digest());
    revisionBytes.encodeSequenceSize(keys.size());
    for (const auto& key : keys) { key.encode(revisionBytes); }
    revisionBytes.encodeSequenceSize(edges.size());
    for (const auto& edge : edges) { revisionBytes.encodeByteString(edge.encodedKey()); }
    auto digest = identity::sha256(revisionBytes.finish().asPtr());
    ZC_REQUIRE(digest != zc::none);
    return VerifiedModuleGraph(zc::heap<VerifiedModuleGraph::Impl>(
        context, fingerprint.clone(), zc::mv(keys), zc::mv(handles), zc::mv(selectedSources),
        zc::mv(edges), ModuleGraphRevision(ZC_ASSERT_NONNULL(digest))));
  }
};

}  // namespace test

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

identity::CompilationUnitIdentity userUnit() {
  return identity::CompilationUnitIdentity::userPackage(package());
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
      zc::Maybe<identity::BuildScriptProducerKey> noOutput;
      auto config = identity::CompilationConfigKey::from(
          identity::CompilationDomain::Target, zc::mv(targetValue),
          identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
      ZC_IF_SOME(configValue, config) {
        auto result = identity::CrateKey::from(userUnit(), identity::CrateTargetKind::Library,
                                               requireScalar<identity::TargetName>("binder"_zc),
                                               zc::mv(configValue));
        ZC_IF_SOME(value, result) { return zc::mv(value); }
      }
    }
  }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

identity::SourceFileKey sourceNamed(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(requireScalar<identity::CanonicalPathSegment>(name));
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return identity::SourceFileKey::from(crate(), identity::SourceOriginKey::localFile(zc::mv(path)));
}

identity::SourceFileKey source() { return sourceNamed("main.zom"_zc); }

identity::SourceFileKey alternateSource() { return sourceNamed("other.zom"_zc); }

identity::ModuleKey moduleNamed(zc::StringPtr name) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>(name));
  auto value = identity::ModuleKey::from(crate(), zc::mv(path));
  ZC_IF_SOME(result, value) { return zc::mv(result); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

identity::ModuleKey qualifiedModule(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>(first));
  path.add(requireScalar<identity::ModulePathSegment>(second));
  auto value = identity::ModuleKey::from(crate(), zc::mv(path));
  ZC_IF_SOME(result, value) { return zc::mv(result); }
  ZC_FAIL_REQUIRE("invalid qualified module test input");
}

identity::ModuleKey module() { return moduleNamed("root"_zc); }

identity::SemanticContextBrand requireContext(identity::SemanticContextFactory& factory) {
  ZC_IF_SOME(result, factory.issue()) { return result; }
  ZC_FAIL_REQUIRE("semantic context test input exhausted");
}

identity::SemanticContextFingerprint fingerprint(
    const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<identity::ToolchainSemanticContextInput> toolchainInputs;
  auto value = identity::SemanticContextFingerprint::compute(
      registries, toolchainInputs.asPtr(), zc::ArrayPtr<const identity::PackageDependencyEdgeKey>(),
      zc::ArrayPtr<const identity::CrateDependencyEdgeKey>());
  ZC_IF_SOME(result, value) { return zc::mv(result); }
  ZC_FAIL_REQUIRE("semantic context fingerprint test input failed");
}

struct ParsedSource final {
  explicit ParsedSource(zc::StringPtr text)
      : sources(zc::heap<source::SourceManager>()),
        diagnostics(zc::heap<diagnostics::DiagnosticEngine>(*sources)),
        buffer(sources->addMemBufferCopy(text.asBytes(), "main.zom")) {
    diagnostics::SourceDiagnosticDraftBuffer parseFacts(*sources, buffer);
    parser::Parser parser(*sources, parseFacts, options, strings, buffer);
    ZC_IF_SOME(parsed, parser.parse()) {
      tree = zc::mv(parsed);
    } else {
      ZC_FAIL_REQUIRE("source fixture did not parse");
    }
    ZC_REQUIRE(!parseFacts.hasErrors());
    auto retainedTokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(retainedTokens != zc::none);
    ZC_IF_SOME(value, retainedTokens) { tokens = zc::mv(value); }
  }

  identity::ImmutableSourceSnapshot snapshotFor(identity::SourceFileKey&& sourceFile) const {
    auto value = identity::ImmutableSourceSnapshot::from(
        zc::mv(sourceFile), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_IF_SOME(result, value) { return zc::mv(result); }
    ZC_FAIL_REQUIRE("source snapshot fixture failed");
  }

  identity::ImmutableSourceSnapshot snapshot() const { return snapshotFor(source()); }

  zc::Own<source::SourceManager> sources;
  zc::Own<diagnostics::DiagnosticEngine> diagnostics;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
};

VerifiedParsedModule alternateVerifiedParsedModule(ParsedSource& sourceFixture) {
  identity::SemanticContextFactory factory;
  const auto context = requireContext(factory);
  auto registriesResult = identity::SemanticIdentityRegistrySet::create(factory, context);
  ZC_REQUIRE(registriesResult != zc::none);
  ZC_IF_SOME(registries, registriesResult) {
    auto snapshot = identity::ImmutableSourceSnapshot::from(
        alternateSource(),
        zc::heapArray(sourceFixture.sources->getEntireTextForBuffer(sourceFixture.buffer)));
    ZC_REQUIRE(snapshot != zc::none);
    ZC_IF_SOME(snapshotValue, snapshot) {
      ZC_REQUIRE(registries.collectCompilationUnit(userUnit()) ==
                 identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.collectSourceFile(snapshotValue.clone()) ==
                 identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(sourceFixture.tokens != zc::none);
      auto retainedTokens = zc::mv(ZC_ASSERT_NONNULL(sourceFixture.tokens));
      return test::requireVerifiedParsedSource(context, registries, snapshotValue,
                                               *sourceFixture.sources, sourceFixture.buffer,
                                               zc::mv(retainedTokens), zc::mv(sourceFixture.tree));
    }
  }
  ZC_FAIL_REQUIRE("alternate parsed module fixture failed");
}

StableIdentityCandidateVerification verifyStableCandidates(ParsedSource& sourceFixture) {
  auto parsed = alternateVerifiedParsedModule(sourceFixture);
  const auto syntax = DefinitionInventory::collect(parsed.tree());
  ZC_REQUIRE(syntax.modules().size() <= 1);
  const ast::NodeId moduleNode =
      syntax.modules().size() == 1 ? syntax.modules()[0].node : ast::NodeId();
  auto production = StableIdentityCandidateProducer::produce(parsed.syntax(), module(), moduleNode);
  return StableIdentityCandidateVerifier::verify(parsed.syntax(), module(), moduleNode, production);
}

void emitStableRedeclarations(ParsedSource& sourceFixture,
                              zc::ArrayPtr<const VerifiedStableDefinitionCandidate> definitions,
                              zc::ArrayPtr<const StableDefinitionRedeclaration> redeclarations) {
  const auto start = sourceFixture.sources->getLocForBufferStart(sourceFixture.buffer);
  for (const auto& redeclaration : redeclarations) {
    ZC_REQUIRE(redeclaration.first < definitions.size());
    ZC_REQUIRE(redeclaration.duplicate < definitions.size());
    const auto& first = definitions[redeclaration.first];
    const auto& duplicate = definitions[redeclaration.duplicate];
    auto name =
        identity::DeclaredDefinitionName::fromCanonical(duplicate.authority.record().name());
    ZC_REQUIRE(name != zc::none);
    ZC_IF_SOME(value, name) {
      ZC_REQUIRE(BindingDiagnosticAdapter::emitRedeclaration(
          *sourceFixture.diagnostics, redeclaration.diagnostic,
          start.getAdvancedLoc(duplicate.source.byteStart()),
          start.getAdvancedLoc(first.source.byteStart()), VerifiedIdentifierArgument::from(value)));
    }
  }
}

void emitStableSourceFailure(ParsedSource& sourceFixture,
                             const StableIdentityCandidateSourceFailure& failure) {
  const auto location = sourceFixture.sources->getLocForBufferStart(sourceFixture.buffer)
                            .getAdvancedLoc(failure.source.byteStart());
  switch (failure.kind) {
    case StableIdentityCandidateSourceFailureKind::ConstantExpressionNotAllowed:
      sourceFixture.diagnostics->diagnose<diagnostics::DiagID::ConstantExpressionNotAllowed>(
          location);
      return;
    case StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter:
      ZC_REQUIRE(failure.previous != zc::none);
      ZC_REQUIRE(failure.identifier != zc::none);
      ZC_IF_SOME(previous, failure.previous) {
        ZC_IF_SOME(identifier, failure.identifier) {
          ZC_REQUIRE(BindingDiagnosticAdapter::emitRedeclaration(
              *sourceFixture.diagnostics, BinderDiagnosticCode::DuplicateIdentifier, location,
              sourceFixture.sources->getLocForBufferStart(sourceFixture.buffer)
                  .getAdvancedLoc(previous.byteStart()),
              VerifiedIdentifierArgument::from(identifier)));
        }
      }
      return;
  }
  ZC_UNREACHABLE;
}

parser::ParsedTokenSnapshot parseTokenSnapshot(source::SourceManager& sources,
                                               const source::BufferId& buffer) {
  diagnostics::SourceDiagnosticDraftBuffer diagnostics(sources, buffer);
  basic::LangOptions options;
  basic::StringPool strings;
  parser::Parser parser(sources, diagnostics, options, strings, buffer);
  ZC_REQUIRE(parser.parse() != zc::none);
  auto tokens = parser.takeTokenSnapshot();
  ZC_REQUIRE(tokens != zc::none);
  ZC_IF_SOME(value, tokens) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("parser token snapshot fixture failed");
}

template <typename Handle>
bool containsIdentityHandle(const zc::Vector<Handle>& handles, Handle candidate) {
  for (const auto handle : handles) {
    if (handle == candidate) { return true; }
  }
  return false;
}

bool containsSyntaxNode(zc::ArrayPtr<const ast::NodeId> nodes, ast::NodeId candidate) {
  for (const auto node : nodes) {
    if (node == candidate) { return true; }
  }
  return false;
}

zc::Maybe<const identity::DefinitionKey&> producedDefinitionKeyAt(
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions, ast::NodeId node) {
  for (const auto& definition : definitions) {
    if (definition.node == node) { return definition.key; }
  }
  return zc::none;
}

zc::Maybe<const identity::ImplKey&> producedImplKeyAt(
    zc::ArrayPtr<const FrozenImplOccurrenceProjection> implementations, ast::NodeId node) {
  for (const auto& implementation : implementations) {
    if (implementation.node == node) { return implementation.key.implementation(); }
  }
  return zc::none;
}

bool hasAuthoritativeImmediateOwner(zc::ArrayPtr<const StructuralIdentityParent> parents,
                                    zc::ArrayPtr<const ast::NodeId> definitionAuthorities,
                                    zc::ArrayPtr<const ast::NodeId> implAuthorities) {
  if (parents.size() == 0) { return false; }
  const auto& owner = parents.back();
  return owner.kind == StructuralIdentityParentKind::Definition
             ? containsSyntaxNode(definitionAuthorities, owner.node)
             : containsSyntaxNode(implAuthorities, owner.node);
}

zc::Maybe<identity::StableGenericParameterOwnerKey> immediateGenericOwner(
    zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const FrozenImplOccurrenceProjection> implementations) {
  if (parents.size() == 0) { return zc::none; }
  const auto& owner = parents.back();
  if (owner.kind == StructuralIdentityParentKind::Definition) {
    ZC_IF_SOME(key, producedDefinitionKeyAt(definitions, owner.node)) {
      return identity::StableGenericParameterOwnerKey::definition(key.clone());
    }
    return zc::none;
  }
  ZC_IF_SOME(key, producedImplKeyAt(implementations, owner.node)) {
    return identity::StableGenericParameterOwnerKey::implementation(key.clone());
  }
  return zc::none;
}

zc::Maybe<const identity::DefinitionKey&> immediateDefinitionOwner(
    zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions) {
  if (parents.size() == 0 || parents.back().kind != StructuralIdentityParentKind::Definition) {
    return zc::none;
  }
  return producedDefinitionKeyAt(definitions, parents.back().node);
}

zc::Maybe<StableBodyOwnerKey> projectedBodyOwner(
    zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const ast::NodeId> definitionAuthorities, const identity::ModuleKey& moduleKey) {
  for (size_t remaining = parents.size(); remaining > 0; --remaining) {
    const auto& parent = parents[remaining - 1];
    if (parent.kind != StructuralIdentityParentKind::Definition) { continue; }
    ZC_IF_SOME(key, producedDefinitionKeyAt(definitions, parent.node)) {
      if (!containsSyntaxNode(definitionAuthorities, parent.node)) { return zc::none; }
      return StableBodyOwnerKey::definition(key.clone());
    }
  }
  return StableBodyOwnerKey::module(moduleKey.clone());
}

zc::Maybe<ast::NodeId> immediateAnonymousOwnerNode(
    const DefinitionInventory& inventory, zc::ArrayPtr<const StructuralIdentityParent> parents) {
  if (parents.size() == 0 || parents.back().kind != StructuralIdentityParentKind::Definition) {
    return zc::none;
  }
  for (const auto& anonymous : inventory.anonymousEntities()) {
    if (anonymous.node == parents.back().node) { return anonymous.node; }
  }
  return zc::none;
}

bool findLocalSyntaxPath(const ast::Tree& tree, ast::NodeId current, ast::NodeId target,
                         zc::Vector<uint32_t>& path) {
  if (current == target) { return true; }
  uint32_t childIndex = 0;
  bool found = false;
  ast::visitChildNodeIds(tree, tree.node(current), [&](ast::NodeId child) {
    const uint32_t currentIndex = childIndex++;
    if (found || !tree.contains(child)) { return; }
    path.add(currentIndex);
    if (findLocalSyntaxPath(tree, child, target, path)) {
      found = true;
      return;
    }
    path.removeLast();
  });
  return found;
}

zc::Maybe<LocalSyntaxPath> localSyntaxPath(const ast::Tree& tree, ast::NodeId owner,
                                           ast::NodeId target) {
  if (!tree.contains(owner) || !tree.contains(target) || owner == target) { return zc::none; }
  zc::Vector<uint32_t> path;
  if (!findLocalSyntaxPath(tree, owner, target, path)) { return zc::none; }
  return LocalSyntaxPath::from(zc::mv(path));
}

zc::Maybe<LocalSyntaxPath> projectedModuleBodyPath(const ast::Tree& tree, ast::NodeId module,
                                                   ast::NodeId target) {
  if (!tree.contains(target) || !tree.contains(tree.root()) ||
      tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return zc::none;
  }

  const auto& sourceFile = tree.node(tree.root());
  ast::NodeList bodyItems;
  if (!module) {
    if (ast::NodeId(sourceFile.payload.words[ast::kSourceFileModuleWord])) { return zc::none; }
    bodyItems = ast::NodeList{sourceFile.payload.words[ast::kSourceFileStatementsFirstWord],
                              sourceFile.payload.words[ast::kSourceFileStatementsSizeWord]};
  } else {
    if (!tree.contains(module) || tree.node(module).kind != ast::SyntaxKind::ModuleDeclaration) {
      return zc::none;
    }
    const auto& declaration = tree.node(module);
    const auto form = static_cast<ast::ModuleDeclarationForm>(
        declaration.payload.words[ast::kModuleDeclarationFormWord]);
    if (form == ast::ModuleDeclarationForm::RootDeclaration) {
      if (ast::NodeId(sourceFile.payload.words[ast::kSourceFileModuleWord]) != module) {
        return zc::none;
      }
      bodyItems = ast::NodeList{sourceFile.payload.words[ast::kSourceFileStatementsFirstWord],
                                sourceFile.payload.words[ast::kSourceFileStatementsSizeWord]};
    } else if (form == ast::ModuleDeclarationForm::InlineRoot) {
      bodyItems =
          ast::NodeList{declaration.payload.words[ast::kModuleDeclarationInlineItemsFirstWord],
                        declaration.payload.words[ast::kModuleDeclarationInlineItemsSizeWord]};
    } else {
      return zc::none;
    }
  }
  if (!tree.contains(bodyItems)) { return zc::none; }

  uint32_t ordinal = 0;
  for (const auto item : tree.list(bodyItems)) {
    zc::Vector<uint32_t> path;
    path.add(ordinal++);
    if (tree.contains(item) && findLocalSyntaxPath(tree, item, target, path)) {
      return LocalSyntaxPath::from(zc::mv(path));
    }
  }
  return zc::none;
}

zc::Maybe<LocalSyntaxPath> projectedBodyPath(
    const ast::Tree& tree, zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const ast::NodeId> definitionAuthorities, ast::NodeId module, ast::NodeId target) {
  for (size_t remaining = parents.size(); remaining > 0; --remaining) {
    const auto& parent = parents[remaining - 1];
    if (parent.kind != StructuralIdentityParentKind::Definition) { continue; }
    if (producedDefinitionKeyAt(definitions, parent.node) == zc::none) { continue; }
    return containsSyntaxNode(definitionAuthorities, parent.node)
               ? localSyntaxPath(tree, parent.node, target)
               : zc::Maybe<LocalSyntaxPath>();
  }
  return projectedModuleBodyPath(tree, module, target);
}

bool isReceiverParameter(const DefinitionInventoryEntry& entry,
                         const VerifiedParsedModule& parsedModule) {
  return parsedModule.functionParameterNameSpan(entry.node, ast::SyntaxKind::ThisKeyword) !=
         zc::none;
}

zc::Maybe<identity::DeclaredDefinitionName> declaredInventoryName(
    const DefinitionInventoryEntry& entry, const VerifiedParsedModule& parsedModule) {
  if (entry.nameKind != InventoryDefinitionNameKind::Declared) { return zc::none; }
  return identity::DeclaredDefinitionName::fromSource(
      parsedModule.tree().ident(entry.declaredName));
}

zc::Maybe<FrozenDefinitionInventoryInput> materializeInventoryInput(
    identity::SemanticContextBrand context, identity::ModuleId module,
    const VerifiedParsedModule& parsedModule, const StableIdentityCandidateInventory& candidates,
    zc::ArrayPtr<const ProducedDefinitionIdentity> admittedDefinitions, bool includeImplementations,
    bool wrongImplSite, identity::SemanticIdentityRegistrySet& registries,
    uint32_t& genericTraversalOrdinal, uint32_t& callableTraversalOrdinal) {
  auto allocator = ModuleLocalIdentityAllocator::create(context, module);
  if (allocator == zc::none) { return zc::none; }
  zc::Maybe<identity::ModuleKey> activeModuleKey;
  ZC_IF_SOME(value, registries.modules().lookup(module)) { activeModuleKey = value.clone(); }
  if (activeModuleKey == zc::none) { return zc::none; }

  FrozenDefinitionInventoryInput input;
  zc::Vector<identity::DefId> definitionHandles;
  zc::Vector<ast::NodeId> definitionAuthorityNodes;
  for (const auto& definition : admittedDefinitions) {
    auto handle = registries.definitions().find(definition.key);
    if (handle == zc::none) { return zc::none; }
    input.definitionCandidates.add(
        ProducedDefinitionIdentity{definition.node, definition.key.clone()});
    ZC_IF_SOME(value, handle) {
      if (!containsIdentityHandle(definitionHandles, value)) {
        definitionHandles.add(value);
        definitionAuthorityNodes.add(definition.node);
      }
    }
  }

  zc::Vector<identity::ImplId> implHandles;
  zc::Vector<ast::NodeId> implAuthorityNodes;
  size_t implCandidateIndex = 0;
  ZC_IF_SOME(allocatorValue, allocator) {
    if (includeImplementations) {
      for (const auto& implementation : candidates.implementations()) {
        while (implCandidateIndex < candidates.candidates().size() &&
               candidates.candidates()[implCandidateIndex].kind() !=
                   PreAdmissionIdentityKind::Implementation) {
          ++implCandidateIndex;
        }
        if (implCandidateIndex == candidates.candidates().size()) { return zc::none; }
        const auto& candidate = candidates.candidates()[implCandidateIndex++];
        auto occurrence = allocatorValue.allocateImplOccurrence();
        auto authority = registries.impls().find(implementation.key);
        if (occurrence == zc::none || authority == zc::none) { return zc::none; }
        auto site = candidate.site().clone();
        if (wrongImplSite) {
          zc::Vector<uint32_t> components;
          for (const auto component : site.moduleSyntaxPath()) { components.add(component); }
          components.add(UINT32_MAX);
          auto mutated =
              IdentitySyntaxSiteKey::from(candidate.site().module().clone(),
                                          candidate.site().source().clone(), zc::mv(components));
          if (mutated == zc::none) { return zc::none; }
          ZC_IF_SOME(value, mutated) { site = zc::mv(value); }
        }
        ZC_IF_SOME(occurrenceValue, occurrence) {
          input.implOccurrences.add(FrozenImplOccurrenceProjection{
              implementation.node, occurrenceValue,
              ImplSourceOccurrenceKey::from(implementation.key.clone(), zc::mv(site))});
        }
        ZC_IF_SOME(authorityValue, authority) {
          if (!containsIdentityHandle(implHandles, authorityValue)) {
            implHandles.add(authorityValue);
            implAuthorityNodes.add(implementation.node);
          }
        }
      }
    }

    const auto syntax = DefinitionInventory::collect(parsedModule.tree());
    const ast::NodeId moduleNode =
        syntax.modules().size() == 1 ? syntax.modules()[0].node : ast::NodeId();
    for (size_t index = 0; index < syntax.genericParameters().size(); ++index) {
      const auto& parameter = syntax.genericParameters()[index];
      if (!hasAuthoritativeImmediateOwner(parameter.parentPath.asPtr(),
                                          definitionAuthorityNodes.asPtr(),
                                          implAuthorityNodes.asPtr())) {
        continue;
      }
      auto owner =
          immediateGenericOwner(parameter.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                                input.implOccurrences.asPtr());
      if (owner == zc::none) { return zc::none; }
      uint32_t ordinal = 0;
      for (size_t prior = 0; prior < index; ++prior) {
        const auto& preceding = syntax.genericParameters()[prior];
        if (!preceding.parentPath.empty() && !parameter.parentPath.empty() &&
            preceding.parentPath.back().node == parameter.parentPath.back().node &&
            hasAuthoritativeImmediateOwner(preceding.parentPath.asPtr(),
                                           definitionAuthorityNodes.asPtr(),
                                           implAuthorityNodes.asPtr())) {
          ++ordinal;
        }
      }
      ZC_IF_SOME(ownerValue, owner) {
        auto record = identity::GenericParameterIdentityRecord::type(zc::mv(ownerValue), ordinal);
        auto key = identity::GenericParameterKey::compute(record);
        if (registries.collectGenericParameter(record.clone(), genericTraversalOrdinal++) !=
            identity::FrozenRegistryFailure::None) {
          return zc::none;
        }
        input.genericParameters.add(FrozenGenericParameterProjection{parameter.node, zc::mv(key)});
      }
    }

    for (size_t index = 0; index < syntax.callableParameters().size(); ++index) {
      const auto& parameter = syntax.callableParameters()[index];
      if (!hasAuthoritativeImmediateOwner(parameter.parentPath.asPtr(),
                                          definitionAuthorityNodes.asPtr(),
                                          implAuthorityNodes.asPtr())) {
        continue;
      }
      auto owner = immediateDefinitionOwner(parameter.parentPath.asPtr(),
                                            input.definitionCandidates.asPtr());
      if (owner == zc::none) { return zc::none; }
      const bool receiver = isReceiverParameter(parameter, parsedModule);
      uint32_t ordinal = 0;
      if (!receiver) {
        for (size_t prior = 0; prior < index; ++prior) {
          const auto& preceding = syntax.callableParameters()[prior];
          if (!preceding.parentPath.empty() && !parameter.parentPath.empty() &&
              preceding.parentPath.back().node == parameter.parentPath.back().node &&
              !isReceiverParameter(preceding, parsedModule) &&
              hasAuthoritativeImmediateOwner(preceding.parentPath.asPtr(),
                                             definitionAuthorityNodes.asPtr(),
                                             implAuthorityNodes.asPtr())) {
            ++ordinal;
          }
        }
      }
      ZC_IF_SOME(ownerValue, owner) {
        auto record = identity::CallableParameterIdentityRecord::from(
            ownerValue.clone(), receiver ? identity::CallableParameterPosition::receiver()
                                         : identity::CallableParameterPosition::ordinary(ordinal));
        auto key = identity::CallableParameterKey::compute(record);
        if (registries.collectCallableParameter(record.clone(), callableTraversalOrdinal++) !=
            identity::FrozenRegistryFailure::None) {
          return zc::none;
        }
        input.callableParameters.add(
            FrozenCallableParameterProjection{parameter.node, zc::mv(key)});
      }
    }

    const auto addOwnerLocalProjection = [&](const DefinitionInventoryEntry& binding,
                                             OwnerLocalBindingNamespace nameSpace,
                                             OwnerLocalBindingKind kind) -> bool {
      auto owner =
          projectedBodyOwner(binding.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                             definitionAuthorityNodes.asPtr(), ZC_ASSERT_NONNULL(activeModuleKey));
      auto path = projectedBodyPath(parsedModule.tree(), binding.parentPath.asPtr(),
                                    input.definitionCandidates.asPtr(),
                                    definitionAuthorityNodes.asPtr(), moduleNode, binding.node);
      auto name = declaredInventoryName(binding, parsedModule);
      auto bindingId = allocatorValue.allocateOwnerLocalBinding();
      if (owner == zc::none || path == zc::none || name == zc::none || bindingId == zc::none) {
        return false;
      }
      ZC_IF_SOME(ownerValue, owner) {
        ZC_IF_SOME(pathValue, path) {
          ZC_IF_SOME(nameValue, name) {
            auto key = OwnerLocalBindingKey::from(zc::mv(ownerValue), zc::mv(pathValue), nameSpace,
                                                  kind, zc::mv(nameValue));
            if (key == zc::none) { return false; }
            ZC_IF_SOME(bindingIdValue, bindingId) {
              ZC_IF_SOME(keyValue, key) {
                input.ownerLocalBindings.add(FrozenOwnerLocalBindingProjection{
                    binding.node, bindingIdValue, zc::mv(keyValue)});
              }
            }
          }
        }
      }
      return true;
    };

    for (const auto& binding : syntax.ownerLocalBindings()) {
      if (!addOwnerLocalProjection(binding, OwnerLocalBindingNamespace::Value,
                                   binding.kind == identity::DefinitionKind::Local
                                       ? OwnerLocalBindingKind::Local
                                       : OwnerLocalBindingKind::PatternBinding)) {
        return zc::none;
      }
    }
    for (const auto& parameter : syntax.genericParameters()) {
      if (immediateAnonymousOwnerNode(syntax, parameter.parentPath.asPtr()) == zc::none) {
        continue;
      }
      if (!addOwnerLocalProjection(parameter, OwnerLocalBindingNamespace::Type,
                                   OwnerLocalBindingKind::GenericParameter)) {
        return zc::none;
      }
    }
    for (const auto& parameter : syntax.callableParameters()) {
      if (immediateAnonymousOwnerNode(syntax, parameter.parentPath.asPtr()) == zc::none) {
        continue;
      }
      if (isReceiverParameter(parameter, parsedModule) ||
          !addOwnerLocalProjection(parameter, OwnerLocalBindingNamespace::Value,
                                   OwnerLocalBindingKind::CallableParameter)) {
        return zc::none;
      }
    }

    for (const auto& anonymous : syntax.anonymousEntities()) {
      auto owner =
          projectedBodyOwner(anonymous.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                             definitionAuthorityNodes.asPtr(), ZC_ASSERT_NONNULL(activeModuleKey));
      auto path = projectedBodyPath(parsedModule.tree(), anonymous.parentPath.asPtr(),
                                    input.definitionCandidates.asPtr(),
                                    definitionAuthorityNodes.asPtr(), moduleNode, anonymous.node);
      if (owner == zc::none || path == zc::none || anonymous.anonymousRole == zc::none) {
        return zc::none;
      }
      ZC_IF_SOME(ownerValue, owner) {
        ZC_IF_SOME(pathValue, path) {
          ZC_IF_SOME(role, anonymous.anonymousRole) {
            auto key =
                AnonymousOwnerLocalKey::from(zc::mv(ownerValue), zc::mv(pathValue),
                                             role == AnonymousSyntaxRole::FunctionExpression
                                                 ? AnonymousOwnerLocalRole::FunctionExpression
                                                 : AnonymousOwnerLocalRole::Closure);
            if (key == zc::none) { return zc::none; }
            ZC_IF_SOME(keyValue, key) {
              input.anonymousEntities.add(
                  FrozenAnonymousEntityProjection{anonymous.node, zc::mv(keyValue)});
            }
          }
        }
      }
    }
  }
  return input;
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
                         ImplRegistration implRegistration = ImplRegistration::None,
                         bool additionalDefinition = false)
      : context(requireContext(factory)), registries(createRegistries()) {
    auto snapshot = sourceFixture.snapshot();
    const auto inventory = DefinitionInventory::collect(sourceFixture.tree);
    ZC_REQUIRE(registries.collectCompilationUnit(userUnit()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(snapshot.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);
    compilationUnitId = requireHandle(registries.compilationUnits().find(userUnit()));
    crateId = requireHandle(registries.crates().find(crate()));
    moduleId = requireHandle(registries.modules().find(module()));

    ZC_REQUIRE(sourceFixture.tokens != zc::none);
    auto retainedTokens = zc::mv(ZC_ASSERT_NONNULL(sourceFixture.tokens));
    parsed = test::requireVerifiedParsedSource(context, registries, snapshot,
                                               *sourceFixture.sources, sourceFixture.buffer,
                                               zc::mv(retainedTokens), zc::mv(sourceFixture.tree));
    ZC_IF_SOME(parsedValue, parsed) {
      const ast::NodeId moduleNode =
          inventory.modules().size() == 0 ? ast::NodeId() : inventory.modules()[0].node;
      auto produced =
          StableIdentityCandidateProducer::produce(parsedValue.syntax(), module(), moduleNode);
      ZC_REQUIRE(produced.is<StableIdentityCandidateInventory>());
      auto candidates = zc::mv(produced.get<StableIdentityCandidateInventory>());

      zc::Vector<ProducedDefinitionIdentity> admittedDefinitions;
      size_t definitionIndex = 0;
      for (const auto& candidate : candidates.candidates()) {
        if (candidate.kind() == PreAdmissionIdentityKind::Definition) {
          ZC_REQUIRE(definitionIndex < candidates.definitions().size());
          if (includeDefinition) {
            auto record = candidate.definitionRecord();
            ZC_REQUIRE(record != zc::none);
            ZC_IF_SOME(value, record) {
              auto admittedRecord = value.clone();
              zc::Maybe<identity::OverloadHeaderAuthority> overload;
              ZC_IF_SOME(authority, candidate.overloadHeader()) { overload = authority.clone(); }
              if (wrongDefinitionKind && definitionIndex == 0 &&
                  value.kind() != identity::DefinitionKind::Class) {
                zc::Vector<identity::EnclosingStableOwnerKey> owners;
                for (const auto& owner : value.owners()) { owners.add(owner.clone()); }
                zc::Maybe<identity::OverloadHeaderDigest> noOverload;
                auto name = identity::DeclaredDefinitionName::fromCanonical(value.name());
                ZC_REQUIRE(name != zc::none);
                ZC_IF_SOME(nameValue, name) {
                  auto mutated = identity::DefinitionIdentityRecord::from(
                      value.module().clone(), zc::mv(owners), identity::DefinitionKind::Class,
                      identity::DefinitionNamespace::Type, zc::mv(nameValue), zc::mv(noOverload));
                  ZC_REQUIRE(mutated != zc::none);
                  ZC_IF_SOME(mutatedValue, mutated) { admittedRecord = zc::mv(mutatedValue); }
                }
                overload = zc::none;
              }
              const auto key = identity::DefinitionKey::compute(admittedRecord);
              ZC_REQUIRE(registries.collectDefinition(admittedRecord.clone(), zc::mv(overload),
                                                      definitionIndex) ==
                         identity::FrozenRegistryFailure::None);
              admittedDefinitions.add(ProducedDefinitionIdentity{
                  candidates.definitions()[definitionIndex].node, key.clone()});
            }
          }
          ++definitionIndex;
          continue;
        }
        if (implRegistration != ImplRegistration::None) {
          auto record = candidate.implRecord();
          ZC_REQUIRE(record != zc::none);
          ZC_IF_SOME(value, record) {
            ZC_REQUIRE(registries.collectImpl(value.clone()) ==
                       identity::FrozenRegistryFailure::None);
          }
        }
      }
      if (additionalDefinition) {
        ZC_REQUIRE(candidates.definitions().size() != 0);
        const auto& sourceCandidate = candidates.candidates()[0];
        auto sourceRecord = sourceCandidate.definitionRecord();
        ZC_REQUIRE(sourceRecord != zc::none);
        ZC_IF_SOME(value, sourceRecord) {
          zc::Vector<identity::EnclosingStableOwnerKey> owners;
          for (const auto& owner : value.owners()) { owners.add(owner.clone()); }
          zc::Maybe<identity::OverloadHeaderDigest> noOverload;
          auto name = identity::DeclaredDefinitionName::fromCanonical(value.name());
          ZC_REQUIRE(name != zc::none);
          ZC_IF_SOME(nameValue, name) {
            auto record = identity::DefinitionIdentityRecord::from(
                value.module().clone(), zc::mv(owners), identity::DefinitionKind::Class,
                identity::DefinitionNamespace::Type, zc::mv(nameValue), zc::mv(noOverload));
            ZC_REQUIRE(record != zc::none);
            ZC_IF_SOME(recordValue, record) {
              zc::Maybe<identity::OverloadHeaderAuthority> noAuthority;
              ZC_REQUIRE(registries.collectDefinition(zc::mv(recordValue), zc::mv(noAuthority),
                                                      definitionIndex) ==
                         identity::FrozenRegistryFailure::None);
            }
          }
        }
      }
      ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
      if (!admittedDefinitions.empty()) {
        definitionId = requireHandle(registries.definitions().find(admittedDefinitions[0].key));
      }
      if (implRegistration != ImplRegistration::None) {
        ZC_REQUIRE(candidates.implementations().size() == 1);
        implId = requireHandle(registries.impls().find(candidates.implementations()[0].key));
      }

      uint32_t genericTraversalOrdinal = 0;
      uint32_t callableTraversalOrdinal = 0;
      auto inventoryInput = materializeInventoryInput(
          context, moduleId, parsedValue, candidates, admittedDefinitions.asPtr(),
          implRegistration != ImplRegistration::None,
          implRegistration == ImplRegistration::WrongOrdinal, registries, genericTraversalOrdinal,
          callableTraversalOrdinal);
      ZC_REQUIRE(inventoryInput != zc::none);
      ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

      ModuleGraphModule modules[] = {ModuleGraphModule(module(), moduleId)};
      auto expectedFingerprint = fingerprint(registries);
      identity::SourceFileKey selectedSources[] = {snapshot.source().clone()};
      auto verifiedGraph = test::VerifiedModuleGraphFixture::build(context, expectedFingerprint,
                                                                   modules, selectedSources);
      graph = verifiedGraph.view(moduleId);
      auto inventoryResult = FrozenDefinitionInventoryVerifier::verifySingleModule(
          context, moduleId, parsedValue, registries, zc::mv(ZC_ASSERT_NONNULL(inventoryInput)));
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
  identity::CompilationUnitId compilationUnitId;
  identity::CrateId crateId;
  identity::ModuleId moduleId;
  identity::DefId definitionId;
  identity::ImplId implId;
  zc::Maybe<VerifiedParsedModule> parsed;
  zc::Maybe<VerifiedModuleGraphView> graph;
  zc::Maybe<FrozenDefinitionInventoryView> frozenDefinitions;
  zc::Maybe<FrozenInventoryInvariantKind> inventoryFailure;
};

struct DependencySurfaceFixture final {
  DependencySurfaceFixture(ParsedSource& requesterSource, ParsedSource& dependencySource,
                           bool qualifiedDependency = false)
      : context(requireContext(factory)), registries(createRegistries()) {
    const auto requesterSourceKey = sourceNamed("app.zom"_zc);
    const auto dependencySourceKey = sourceNamed("dep.zom"_zc);
    const auto requesterModuleKey = moduleNamed("app"_zc);
    const auto dependencyModuleKey = [&]() {
      if (qualifiedDependency) { return qualifiedModule("dep"_zc, "core"_zc); }
      return moduleNamed("dep"_zc);
    }();
    zc::Maybe<identity::ModuleKey> dependencyRootModuleKey;
    if (qualifiedDependency) { dependencyRootModuleKey = moduleNamed("dep"_zc); }
    auto requesterSnapshot = requesterSource.snapshotFor(requesterSourceKey.clone());
    auto dependencySnapshot = dependencySource.snapshotFor(dependencySourceKey.clone());
    const auto requesterInventory = DefinitionInventory::collect(requesterSource.tree);
    const auto dependencyInventory = DefinitionInventory::collect(dependencySource.tree);

    ZC_REQUIRE(registries.collectCompilationUnit(userUnit()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(requesterSnapshot.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(dependencySnapshot.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(requesterModuleKey.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(dependencyModuleKey.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_IF_SOME(root, dependencyRootModuleKey) {
      ZC_REQUIRE(registries.collectModule(root.clone()) == identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);

    compilationUnitId = requireHandle(registries.compilationUnits().find(userUnit()));
    crateId = requireHandle(registries.crates().find(crate()));
    requesterModule = requireHandle(registries.modules().find(requesterModuleKey));
    dependencyModule = requireHandle(registries.modules().find(dependencyModuleKey));
    zc::Maybe<identity::ModuleId> dependencyRootModule;
    ZC_IF_SOME(root, dependencyRootModuleKey) {
      dependencyRootModule = requireHandle(registries.modules().find(root));
    }
    requesterParsed = promote(requesterSource, requesterSnapshot);
    dependencyParsed = promote(dependencySource, dependencySnapshot);

    ZC_REQUIRE(requesterParsed != zc::none);
    ZC_REQUIRE(dependencyParsed != zc::none);
    const ast::NodeId requesterModuleNode = requesterInventory.modules().size() == 0
                                                ? ast::NodeId()
                                                : requesterInventory.modules()[0].node;
    const ast::NodeId dependencyModuleNode = dependencyInventory.modules().size() == 0
                                                 ? ast::NodeId()
                                                 : dependencyInventory.modules()[0].node;
    auto requesterProduction = StableIdentityCandidateProducer::produce(
        ZC_ASSERT_NONNULL(requesterParsed).syntax(), requesterModuleKey, requesterModuleNode);
    auto dependencyProduction = StableIdentityCandidateProducer::produce(
        ZC_ASSERT_NONNULL(dependencyParsed).syntax(), dependencyModuleKey, dependencyModuleNode);
    ZC_REQUIRE(requesterProduction.is<StableIdentityCandidateInventory>());
    ZC_REQUIRE(dependencyProduction.is<StableIdentityCandidateInventory>());
    auto requesterCandidates = zc::mv(requesterProduction.get<StableIdentityCandidateInventory>());
    auto dependencyCandidates =
        zc::mv(dependencyProduction.get<StableIdentityCandidateInventory>());

    uint32_t stableTraversalOrdinal = 0;
    const auto collectStable = [&](const StableIdentityCandidateInventory& candidates) {
      for (const auto& candidate : candidates.candidates()) {
        if (candidate.kind() == PreAdmissionIdentityKind::Definition) {
          auto record = candidate.definitionRecord();
          ZC_REQUIRE(record != zc::none);
          zc::Maybe<identity::OverloadHeaderAuthority> overload;
          ZC_IF_SOME(value, candidate.overloadHeader()) { overload = value.clone(); }
          ZC_IF_SOME(value, record) {
            ZC_REQUIRE(registries.collectDefinition(value.clone(), zc::mv(overload),
                                                    stableTraversalOrdinal++) ==
                       identity::FrozenRegistryFailure::None);
          }
          continue;
        }
        auto record = candidate.implRecord();
        ZC_REQUIRE(record != zc::none);
        ZC_IF_SOME(value, record) {
          ZC_REQUIRE(registries.collectImpl(value.clone(), stableTraversalOrdinal++) ==
                     identity::FrozenRegistryFailure::None);
        }
      }
    };
    collectStable(requesterCandidates);
    collectStable(dependencyCandidates);
    ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);

    zc::Vector<ProducedDefinitionIdentity> requesterDefinitions;
    for (const auto& definition : requesterCandidates.definitions()) {
      requesterDefinitions.add(ProducedDefinitionIdentity{definition.node, definition.key.clone()});
    }
    zc::Vector<ProducedDefinitionIdentity> dependencyDefinitions;
    for (const auto& definition : dependencyCandidates.definitions()) {
      dependencyDefinitions.add(
          ProducedDefinitionIdentity{definition.node, definition.key.clone()});
    }
    uint32_t genericTraversalOrdinal = 0;
    uint32_t callableTraversalOrdinal = 0;
    auto requesterInventoryInput = materializeInventoryInput(
        context, requesterModule, ZC_ASSERT_NONNULL(requesterParsed), requesterCandidates,
        requesterDefinitions.asPtr(), requesterCandidates.implementations().size() != 0, false,
        registries, genericTraversalOrdinal, callableTraversalOrdinal);
    auto dependencyInventoryInput = materializeInventoryInput(
        context, dependencyModule, ZC_ASSERT_NONNULL(dependencyParsed), dependencyCandidates,
        dependencyDefinitions.asPtr(), dependencyCandidates.implementations().size() != 0, false,
        registries, genericTraversalOrdinal, callableTraversalOrdinal);
    ZC_REQUIRE(requesterInventoryInput != zc::none);
    ZC_REQUIRE(dependencyInventoryInput != zc::none);
    ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    zc::Vector<identity::CanonicalPathSegment> noRootSegments;
    zc::Vector<ModuleSearchRoot> searchRoots;
    searchRoots.add(ModuleSearchRoot::workspace(
        crate(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(noRootSegments))));
    zc::Vector<ModuleSourceSnapshotRevision> snapshotRevisions;
    snapshotRevisions.add(ModuleSourceSnapshotRevision(requesterSourceKey.clone(),
                                                       requesterSnapshot.contentDigest()));
    snapshotRevisions.add(ModuleSourceSnapshotRevision(dependencySourceKey.clone(),
                                                       dependencySnapshot.contentDigest()));
    zc::Vector<GeneratedModuleSourceRevision> generated;
    zc::Vector<ModuleDependencyAliasRoot> aliases;
    zc::Vector<identity::ModuleKey> requesterPath;
    requesterPath.add(requesterModuleKey.clone());
    zc::Vector<identity::ModuleKey> dependencyPath;
    dependencyPath.add(dependencyModuleKey.clone());
    ZC_IF_SOME(root, dependencyRootModuleKey) { dependencyPath.add(root.clone()); }
    zc::Vector<RequesterModuleAncestryCandidate> ancestry;
    ancestry.add(
        RequesterModuleAncestryCandidate(requesterModuleKey.clone(), zc::mv(requesterPath)));
    ancestry.add(
        RequesterModuleAncestryCandidate(dependencyModuleKey.clone(), zc::mv(dependencyPath)));
    ZC_IF_SOME(root, dependencyRootModuleKey) {
      zc::Vector<identity::ModuleKey> rootPath;
      rootPath.add(root.clone());
      ancestry.add(RequesterModuleAncestryCandidate(root.clone(), zc::mv(rootPath)));
    }
    zc::Vector<StructuralModuleCatalogEntry> catalog;
    catalog.add(StructuralModuleCatalogEntry(requesterModuleKey.clone(), requesterModule,
                                             requesterSourceKey.clone()));
    catalog.add(StructuralModuleCatalogEntry(dependencyModuleKey.clone(), dependencyModule,
                                             dependencySourceKey.clone()));
    ZC_IF_SOME(root, dependencyRootModuleKey) {
      ZC_IF_SOME(rootModule, dependencyRootModule) {
        catalog.add(
            StructuralModuleCatalogEntry(root.clone(), rootModule, dependencySourceKey.clone()));
      }
    }
    auto resolverResult = StructuralModuleResolver::freeze(
        context, registries,
        ModuleResolutionEnvironmentRecord(zc::mv(searchRoots), zc::mv(snapshotRevisions),
                                          zc::mv(generated), zc::mv(aliases), zc::mv(ancestry)),
        zc::mv(catalog));
    ZC_REQUIRE(resolverResult.is<StructuralModuleResolver>());
    auto resolver = zc::mv(resolverResult.get<StructuralModuleResolver>());

    ZC_IF_SOME(requesterParsedValue, requesterParsed) {
      auto requests =
          ModuleDependencyRequestDeriver::derive(requesterModule, requesterParsedValue, resolver);
      ZC_REQUIRE(requests.is<zc::Vector<ModuleDependencyRequest>>());
      auto requestValues = zc::mv(requests.get<zc::Vector<ModuleDependencyRequest>>());
      basic::ThreadPool scheduler(2);
      query::QueryDatabase database(scheduler);
      ZC_REQUIRE(
          driver::incremental_module_resolution_query::registerIncrementalModuleResolutionQueries(
              database));
      auto pending = database.beginInputTransaction();
      ZC_REQUIRE(pending != zc::none);
      ZC_IF_SOME(transaction, pending) {
        ZC_REQUIRE(driver::incremental_module_resolution_query::stageModuleResolutionQueryInputs(
            transaction, resolver, requestValues.asPtr()));
        ZC_REQUIRE(transaction.commit() != zc::none);
      }
      auto resolutionSnapshot = database.snapshot();
      zc::Vector<identity::ModuleId> targets(requestValues.size());
      for (const auto& request : requestValues) {
        auto candidates =
            resolutionSnapshot
                .get<driver::incremental_module_resolution_query::ResolveModuleRequestQuery>(
                    request.key());
        ZC_REQUIRE(!candidates.isRuntimeFailure());
        ZC_REQUIRE(candidates.kind() == query::QueryValueKind::Value);
        ZC_REQUIRE(candidates.value().candidates().size() == 1);
        auto target = registries.modules().find(candidates.value().candidates()[0]);
        ZC_REQUIRE(target != zc::none);
        targets.add(ZC_ASSERT_NONNULL(target));
      }
      ZC_IF_SOME(dependencyParsedValue, dependencyParsed) {
        zc::Vector<ModuleGraphModule> modules;
        modules.add(ModuleGraphModule(requesterModuleKey.clone(), requesterModule));
        modules.add(ModuleGraphModule(dependencyModuleKey.clone(), dependencyModule));
        ZC_IF_SOME(root, dependencyRootModuleKey) {
          ZC_IF_SOME(rootModule, dependencyRootModule) {
            modules.add(ModuleGraphModule(root.clone(), rootModule));
          }
        }
        auto expectedFingerprint = fingerprint(registries);
        zc::Vector<identity::SourceFileKey> selectedSources;
        selectedSources.add(requesterSourceKey.clone());
        selectedSources.add(dependencySourceKey.clone());
        if (dependencyRootModuleKey != zc::none) {
          selectedSources.add(dependencySourceKey.clone());
        }
        auto graph = test::VerifiedModuleGraphFixture::build(
            context, expectedFingerprint, modules.asPtr(), selectedSources.asPtr(),
            requestValues.asPtr(), targets.asPtr());
        requesterGraph = graph.view(requesterModule);
        dependencyGraph = graph.view(dependencyModule);

        auto requesterDefinitions = FrozenDefinitionInventoryVerifier::verifySingleModule(
            context, requesterModule, requesterParsedValue, registries,
            zc::mv(ZC_ASSERT_NONNULL(requesterInventoryInput)));
        auto dependencyDefinitions = FrozenDefinitionInventoryVerifier::verifySingleModule(
            context, dependencyModule, dependencyParsedValue, registries,
            zc::mv(ZC_ASSERT_NONNULL(dependencyInventoryInput)));
        ZC_REQUIRE(requesterDefinitions.is<FrozenDefinitionInventoryView>());
        ZC_REQUIRE(dependencyDefinitions.is<FrozenDefinitionInventoryView>());
        requesterFrozenDefinitions =
            zc::mv(requesterDefinitions.get<FrozenDefinitionInventoryView>());
        dependencyFrozenDefinitions =
            zc::mv(dependencyDefinitions.get<FrozenDefinitionInventoryView>());
      }
    }

    auto dependencyInputResult = verifyDependency();
    ZC_REQUIRE(dependencyInputResult.is<VerifiedBindingInput>());
    auto dependencyInput = zc::mv(dependencyInputResult.get<VerifiedBindingInput>());
    auto binding = BindingBuilder::build(dependencyInput, *dependencySource.diagnostics);
    ZC_REQUIRE(binding.is<BindingMetadataCandidate>());
    auto output =
        BindingVerifier::verify(dependencyInput, zc::mv(binding.get<BindingMetadataCandidate>()));
    ZC_REQUIRE(output.is<VerifiedBindingOutput>());
    dependencySurface = zc::mv(output.get<VerifiedBindingOutput>().surface);
  }

  BindingInputVerificationResult verifyDependency() {
    ZC_IF_SOME(parsed, dependencyParsed) {
      ZC_IF_SOME(graph, dependencyGraph) {
        ZC_IF_SOME(definitions, dependencyFrozenDefinitions) {
          return BindingInputVerifier::verify(BindingInputCandidate{
              context, compilationUnitId, crateId, dependencyModule, registries, graph, parsed,
              definitions, zc::ArrayPtr<const DependencyExportSurface>(), zc::none});
        }
      }
    }
    ZC_FAIL_REQUIRE("dependency binding input fixture is incomplete");
  }

  BindingInputVerificationResult verifyRequesterWith(
      zc::ArrayPtr<const DependencyExportSurface> dependencySurfaces) {
    ZC_IF_SOME(parsed, requesterParsed) {
      ZC_IF_SOME(graph, requesterGraph) {
        ZC_IF_SOME(definitions, requesterFrozenDefinitions) {
          return BindingInputVerifier::verify(BindingInputCandidate{
              context, compilationUnitId, crateId, requesterModule, registries, graph, parsed,
              definitions, dependencySurfaces, zc::none});
        }
      }
    }
    ZC_FAIL_REQUIRE("requester binding input fixture is incomplete");
  }

  BindingInputVerificationResult verifyRequester() {
    ZC_IF_SOME(surface, dependencySurface) {
      DependencyExportSurface surfaces[] = {{dependencyModule, surface}};
      return verifyRequesterWith(surfaces);
    }
    ZC_FAIL_REQUIRE("dependency export surface fixture is incomplete");
  }

  template <typename Handle>
  Handle requireHandle(zc::Maybe<Handle>&& value) {
    ZC_IF_SOME(result, value) { return result; }
    ZC_FAIL_REQUIRE("dependency identity handle lookup failed");
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    ZC_IF_SOME(result, identity::SemanticIdentityRegistrySet::create(factory, context)) {
      return zc::mv(result);
    }
    ZC_FAIL_REQUIRE("dependency registry set test input was already claimed");
  }

  VerifiedParsedModule promote(ParsedSource& sourceFixture,
                               const identity::ImmutableSourceSnapshot& snapshot) {
    ZC_REQUIRE(sourceFixture.tokens != zc::none);
    auto retainedTokens = zc::mv(ZC_ASSERT_NONNULL(sourceFixture.tokens));
    return test::requireVerifiedParsedSource(context, registries, snapshot, *sourceFixture.sources,
                                             sourceFixture.buffer, zc::mv(retainedTokens),
                                             zc::mv(sourceFixture.tree));
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  identity::CompilationUnitId compilationUnitId;
  identity::CrateId crateId;
  identity::ModuleId requesterModule;
  identity::ModuleId dependencyModule;
  zc::Maybe<VerifiedParsedModule> requesterParsed;
  zc::Maybe<VerifiedParsedModule> dependencyParsed;
  zc::Maybe<VerifiedModuleGraphView> requesterGraph;
  zc::Maybe<VerifiedModuleGraphView> dependencyGraph;
  zc::Maybe<FrozenDefinitionInventoryView> requesterFrozenDefinitions;
  zc::Maybe<FrozenDefinitionInventoryView> dependencyFrozenDefinitions;
  zc::Maybe<VerifiedExportSurface> dependencySurface;
};

BindingInputVerificationResult verify(FrozenFixture& fixture) {
  ZC_IF_SOME(parsed, fixture.parsed) {
    ZC_IF_SOME(graph, fixture.graph) {
      ZC_IF_SOME(definitions, fixture.frozenDefinitions) {
        return BindingInputVerifier::verify(
            BindingInputCandidate{fixture.context, fixture.compilationUnitId, fixture.crateId,
                                  fixture.moduleId, fixture.registries, graph, parsed, definitions,
                                  zc::ArrayPtr<const DependencyExportSurface>(), zc::none});
      }
    }
  }
  ZC_FAIL_REQUIRE("binding verification requires complete verified inputs");
}

class ModuleGraphDiagnosticCapture final : public diagnostics::DiagnosticConsumer {
public:
  size_t count = 0;
  diagnostics::DiagID id = diagnostics::DiagID::UndefinedIdentifier;
  source::SourceLoc primary;
  size_t argumentCount = 0;
  zc::String argument;
  zc::String rendered;
  zc::Vector<source::CharSourceRange> ranges;

  void handleDiagnostic(const source::SourceManager& sources,
                        const diagnostics::Diagnostic& diagnostic) override {
    ++count;
    id = diagnostic.getId();
    primary = diagnostic.getLoc();
    argumentCount = diagnostic.getArgs().size();
    if (argumentCount == 1) {
      const auto& value = diagnostic.getArgs()[0];
      if (value.is<zc::String>()) {
        argument = zc::str(value.get<zc::String>());
      } else if (value.is<zc::StringPtr>()) {
        argument = zc::str(value.get<zc::StringPtr>());
      }
    }
    for (const auto& range : diagnostic.getRanges()) { ranges.add(range); }
    zc::VectorOutputStream output;
    diagnostics::DiagnosticEngine::formatDiagnosticMessage(
        sources, output, diagnostics::getDiagnosticInfo(id).message, diagnostic.getArgs());
    rendered = zc::str(output.getArray().asChars());
  }
};

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

BindingTarget requireScopeBindingTarget(zc::ArrayPtr<const ScopeRecord> scopes, zc::StringPtr name,
                                        size_t occurrence = 0) {
  size_t matching = 0;
  for (const auto& scope : scopes) {
    for (const auto& binding : scope.bindings) {
      if (binding.name.nameSpace() != Namespace::Value || binding.name.name().text() != name) {
        continue;
      }
      if (matching++ == occurrence) { return binding.binding.bindingIdentity.clone(); }
    }
  }
  ZC_FAIL_REQUIRE("named scope binding target is missing");
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

const BoundThis& requireThisBinding(zc::ArrayPtr<const BoundThis> bindings, ast::NodeId node) {
  for (const auto& binding : bindings) {
    if (binding.expression == node) { return binding; }
  }
  ZC_FAIL_REQUIRE("this binding is missing");
}

BoundThis& requireThisBinding(zc::ArrayPtr<BoundThis> bindings, ast::NodeId node) {
  for (auto& binding : bindings) {
    if (binding.expression == node) { return binding; }
  }
  ZC_FAIL_REQUIRE("mutable this binding is missing");
}

const BoundSelfType& requireSelfType(zc::ArrayPtr<const BoundSelfType> facts, ast::NodeId node) {
  for (const auto& fact : facts) {
    if (fact.syntax == node) { return fact; }
  }
  ZC_FAIL_REQUIRE("contextual Self fact is missing");
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

MemberVisibility requireMemberVisibility(const DefinitionFact& fact) {
  ZC_REQUIRE(fact.memberVisibility != zc::none);
  ZC_IF_SOME(value, fact.memberVisibility) { return value; }
  ZC_FAIL_REQUIRE("member visibility fact is missing");
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

identity::CallableParameterId requireCallableParameterAt(const VerifiedBindingInput& input,
                                                         ast::NodeId node) {
  auto parameter = input.definitions().callableParameterAt(node);
  ZC_IF_SOME(value, parameter) { return value; }
  ZC_FAIL_REQUIRE("callable parameter identity is missing at syntax node");
}

OwnerLocalBindingId requireOwnerLocalBindingAt(const VerifiedBindingInput& input,
                                               ast::NodeId node) {
  auto binding = input.definitions().ownerLocalBindingAt(node);
  ZC_IF_SOME(value, binding) { return value; }
  ZC_FAIL_REQUIRE("owner-local binding identity is missing at syntax node");
}

const AnonymousOwnerLocalKey& requireAnonymousAt(const VerifiedBindingInput& input,
                                                 ast::NodeId node) {
  auto entity = input.definitions().anonymousEntityAt(node);
  ZC_IF_SOME(value, entity) { return value.key; }
  ZC_FAIL_REQUIRE("anonymous owner-local key is missing at syntax node");
}

const FrozenOwnerLocalBindingEntry& requireNamedOwnerLocalEntry(const VerifiedBindingInput& input,
                                                                zc::StringPtr name,
                                                                OwnerLocalBindingKind kind) {
  for (const auto& entry : input.definitions().ownerLocalBindings()) {
    if (entry.key.kind() == kind && entry.key.name().text() == name) { return entry; }
  }
  ZC_FAIL_REQUIRE("named frozen owner-local binding is missing");
}

identity::DefId requireNamedFrozenDefinition(const VerifiedBindingInput& input, zc::StringPtr name,
                                             identity::DefinitionKind kind, size_t occurrence = 0) {
  size_t matching = 0;
  for (const auto& entry : input.definitions().definitions()) {
    if (entry.record.kind() != kind || entry.bindingName == zc::none) { continue; }
    ZC_IF_SOME(bindingName, entry.bindingName) {
      if (bindingName.text() == name && matching++ == occurrence) { return entry.definition; }
    }
  }
  ZC_FAIL_REQUIRE("named frozen definition is missing");
}

BindingTarget requireNamedFrozenBindingTarget(const VerifiedBindingInput& input, zc::StringPtr name,
                                              identity::DefinitionKind kind,
                                              size_t occurrence = 0) {
  size_t matching = 0;
  if (kind == identity::DefinitionKind::Parameter) {
    for (const auto& entry : input.definitions().callableParameters()) {
      if (entry.bindingName == zc::none) { continue; }
      ZC_IF_SOME(bindingName, entry.bindingName) {
        if (bindingName.text() == name && matching++ == occurrence) {
          return BindingTarget::callableParameter(entry.parameter);
        }
      }
    }
    for (const auto& entry : input.definitions().ownerLocalBindings()) {
      if (entry.key.kind() == OwnerLocalBindingKind::CallableParameter &&
          entry.key.name().text() == name && matching++ == occurrence) {
        return BindingTarget::ownerLocal(entry.binding);
      }
    }
  } else if (kind == identity::DefinitionKind::TypeParameter) {
    for (const auto& entry : input.definitions().genericParameters()) {
      if (entry.bindingName.text() == name && matching++ == occurrence) {
        return BindingTarget::genericParameter(entry.parameter);
      }
    }
    for (const auto& entry : input.definitions().ownerLocalBindings()) {
      if (entry.key.kind() == OwnerLocalBindingKind::GenericParameter &&
          entry.key.name().text() == name && matching++ == occurrence) {
        return BindingTarget::ownerLocal(entry.binding);
      }
    }
  } else if (kind == identity::DefinitionKind::Local ||
             kind == identity::DefinitionKind::PatternBinding) {
    const auto expectedKind = kind == identity::DefinitionKind::Local
                                  ? OwnerLocalBindingKind::Local
                                  : OwnerLocalBindingKind::PatternBinding;
    for (const auto& entry : input.definitions().ownerLocalBindings()) {
      if (entry.key.kind() == expectedKind && entry.key.name().text() == name &&
          matching++ == occurrence) {
        return BindingTarget::ownerLocal(entry.binding);
      }
    }
  } else {
    return BindingTarget::definition(requireNamedFrozenDefinition(input, name, kind, occurrence));
  }
  ZC_FAIL_REQUIRE("named frozen binding target is missing");
}

bool sameBindingTargetForTest(const BindingTarget& left, const BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DefinitionBindingTarget>()) {
    return rightValue.is<DefinitionBindingTarget>() &&
           leftValue.get<DefinitionBindingTarget>().definition ==
               rightValue.get<DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<GenericParameterBindingTarget>()) {
    return rightValue.is<GenericParameterBindingTarget>() &&
           leftValue.get<GenericParameterBindingTarget>().parameter ==
               rightValue.get<GenericParameterBindingTarget>().parameter;
  }
  if (leftValue.is<CallableParameterBindingTarget>()) {
    return rightValue.is<CallableParameterBindingTarget>() &&
           leftValue.get<CallableParameterBindingTarget>().parameter ==
               rightValue.get<CallableParameterBindingTarget>().parameter;
  }
  if (leftValue.is<OwnerLocalBindingTarget>()) {
    return rightValue.is<OwnerLocalBindingTarget>() &&
           leftValue.get<OwnerLocalBindingTarget>().binding ==
               rightValue.get<OwnerLocalBindingTarget>().binding;
  }
  if (leftValue.is<SemanticImportBindingTarget>()) {
    return rightValue.is<SemanticImportBindingTarget>() &&
           leftValue.get<SemanticImportBindingTarget>().binding ==
               rightValue.get<SemanticImportBindingTarget>().binding;
  }
  return rightValue.is<ModuleBindingTarget>() && leftValue.get<ModuleBindingTarget>().module ==
                                                     rightValue.get<ModuleBindingTarget>().module;
}

GenericParameterFact cloneGenericParameterFact(const GenericParameterFact& fact) {
  return GenericParameterFact{fact.identity, fact.site.clone(), fact.name.clone(),
                              fact.declaringScope, fact.source.clone()};
}

CallableParameterFact cloneCallableParameterFact(const CallableParameterFact& fact) {
  zc::Maybe<identity::DeclaredDefinitionName> name;
  ZC_IF_SOME(value, fact.name) { name = value.clone(); }
  return CallableParameterFact{fact.identity,       fact.site.clone(),   zc::mv(name),
                               fact.declaringScope, fact.source.clone(), fact.receiver};
}

OwnerLocalBindingFact cloneOwnerLocalBindingFact(const OwnerLocalBindingFact& fact) {
  return OwnerLocalBindingFact{fact.identity,       fact.site.clone(), fact.kind,
                               fact.name.clone(),   fact.nameSpace,    fact.declaringScope,
                               fact.source.clone(), fact.activation};
}

const OwnerLocalBindingFact& requireOwnerLocalBindingFact(
    zc::ArrayPtr<const OwnerLocalBindingFact> facts, const BindingTarget& target) {
  ZC_REQUIRE(target.value().is<OwnerLocalBindingTarget>());
  const auto binding = target.value().get<OwnerLocalBindingTarget>().binding;
  for (const auto& fact : facts) {
    if (fact.identity == binding) { return fact; }
  }
  ZC_FAIL_REQUIRE("owner-local binding fact is missing");
}

const ClosureFreeVariableFact& requireClosureFreeVariable(
    zc::ArrayPtr<const ClosureFreeVariableFact> facts, const AnonymousOwnerLocalKey& closure) {
  for (const auto& fact : facts) {
    if (fact.closure == closure) { return fact; }
  }
  ZC_FAIL_REQUIRE("closure free-variable fact is missing");
}

ClosureFreeVariableFact& requireClosureFreeVariable(zc::ArrayPtr<ClosureFreeVariableFact> facts,
                                                    const AnonymousOwnerLocalKey& closure) {
  for (auto& fact : facts) {
    if (fact.closure == closure) { return fact; }
  }
  ZC_FAIL_REQUIRE("mutable closure free-variable fact is missing");
}

const FreeVariableFact& requireFreeVariable(const ClosureFreeVariableFact& closure,
                                            const BindingTarget& target) {
  for (const auto& variable : closure.variables) {
    if (sameBindingTargetForTest(variable.target, target)) { return variable; }
  }
  ZC_FAIL_REQUIRE("free-variable target is missing");
}

FreeVariableFact& requireFreeVariable(ClosureFreeVariableFact& closure,
                                      const BindingTarget& target) {
  for (auto& variable : closure.variables) {
    if (sameBindingTargetForTest(variable.target, target)) { return variable; }
  }
  ZC_FAIL_REQUIRE("mutable free-variable target is missing");
}

FreeVariableFact cloneFreeVariable(const FreeVariableFact& fact) {
  zc::Vector<ast::NodeId> sites;
  for (const auto site : fact.referenceSites) { sites.add(site); }
  return FreeVariableFact{fact.target.clone(), zc::mv(sites)};
}

ClosureFreeVariableFact cloneClosureFreeVariable(const ClosureFreeVariableFact& fact) {
  zc::Vector<FreeVariableFact> variables;
  for (const auto& variable : fact.variables) { variables.add(cloneFreeVariable(variable)); }
  return ClosureFreeVariableFact{fact.closure.clone(), zc::mv(variables)};
}

const ExplicitClosureCaptureFact& requireExplicitClosureCapture(
    zc::ArrayPtr<const ExplicitClosureCaptureFact> facts, const AnonymousOwnerLocalKey& closure) {
  for (const auto& fact : facts) {
    if (fact.closure == closure) { return fact; }
  }
  ZC_FAIL_REQUIRE("explicit closure capture fact is missing");
}

ExplicitClosureCaptureFact& requireExplicitClosureCapture(
    zc::ArrayPtr<ExplicitClosureCaptureFact> facts, const AnonymousOwnerLocalKey& closure) {
  for (auto& fact : facts) {
    if (fact.closure == closure) { return fact; }
  }
  ZC_FAIL_REQUIRE("mutable explicit closure capture fact is missing");
}

ExplicitCaptureBindingFact cloneExplicitCapture(const ExplicitCaptureBindingFact& fact) {
  return ExplicitCaptureBindingFact{fact.item, fact.target.clone(), fact.source.clone()};
}

ExplicitClosureCaptureFact cloneExplicitClosureCapture(const ExplicitClosureCaptureFact& fact) {
  zc::Vector<ExplicitCaptureBindingFact> captures;
  for (const auto& capture : fact.captures) { captures.add(cloneExplicitCapture(capture)); }
  return ExplicitClosureCaptureFact{fact.closure.clone(), fact.captureList, fact.source.clone(),
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
    const auto previous = facts[index - 1].closure.encode();
    const auto current = facts[index].closure.encode();
    ZC_EXPECT(encodedBytesLess(previous.asPtr(), current.asPtr()));
  }
  for (const auto& closure : facts) {
    for (size_t index = 1; index < closure.variables.size(); ++index) {
      auto encodeTarget = [&](const BindingTarget& target) -> zc::Maybe<zc::Array<uint8_t>> {
        identity::CanonicalEncoder encoder;
        const auto& value = target.value();
        if (value.is<DefinitionBindingTarget>()) {
          ZC_IF_SOME(key, input.definitions().definitionKey(
                              value.get<DefinitionBindingTarget>().definition)) {
            encoder.encodeUint8(0x01);
            key.encode(encoder);
            return encoder.finish();
          }
          return zc::none;
        }
        if (value.is<GenericParameterBindingTarget>()) {
          ZC_IF_SOME(key, input.definitions().genericParameterKey(
                              value.get<GenericParameterBindingTarget>().parameter)) {
            encoder.encodeUint8(0x02);
            key.encode(encoder);
            return encoder.finish();
          }
          return zc::none;
        }
        if (value.is<CallableParameterBindingTarget>()) {
          ZC_IF_SOME(key, input.definitions().callableParameterKey(
                              value.get<CallableParameterBindingTarget>().parameter)) {
            encoder.encodeUint8(0x03);
            key.encode(encoder);
            return encoder.finish();
          }
          return zc::none;
        }
        if (value.is<OwnerLocalBindingTarget>()) {
          for (const auto& entry : input.definitions().ownerLocalBindings()) {
            if (entry.binding != value.get<OwnerLocalBindingTarget>().binding) { continue; }
            encoder.encodeUint8(0x04);
            entry.key.encode(encoder);
            return encoder.finish();
          }
          return zc::none;
        }
        if (value.is<SemanticImportBindingTarget>()) {
          encoder.encodeUint8(0x05);
          encoder.encodeByteString(
              value.get<SemanticImportBindingTarget>().binding.encode().asPtr());
          return encoder.finish();
        }
        ZC_IF_SOME(key, input.moduleKey(value.get<ModuleBindingTarget>().module)) {
          encoder.encodeUint8(0x06);
          key.encode(encoder);
          return encoder.finish();
        }
        return zc::none;
      };
      auto previous = encodeTarget(closure.variables[index - 1].target);
      auto current = encodeTarget(closure.variables[index].target);
      ZC_REQUIRE(previous != zc::none);
      ZC_REQUIRE(current != zc::none);
      ZC_EXPECT(encodedBytesLess(ZC_ASSERT_NONNULL(previous).asPtr(),
                                 ZC_ASSERT_NONNULL(current).asPtr()));
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
                  "56c0a5afb5b2e90e2acca2850b829b99ed6dd8dfb6812ac4ad81001eb0e96bfd"_zc);
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
        ZC_EXPECT(sourceFixture.sources->getLocOffsetInBuffer(value, sourceFixture.buffer) ==
                  span.byteStart());
      }
    }
    ZC_IF_SOME(span, keyword) {
      ZC_EXPECT(span.byteEnd() - span.byteStart() == 10);
      auto location = parsed.sourceLocFor(span);
      ZC_REQUIRE(location != zc::none);
      ZC_IF_SOME(value, location) {
        ZC_EXPECT(sourceFixture.sources->getLocOffsetInBuffer(value, sourceFixture.buffer) ==
                  span.byteStart());
      }
    }
    ZC_IF_SOME(span, reference) {
      ZC_EXPECT(span.byteEnd() - span.byteStart() == 10);
      auto location = parsed.sourceLocFor(span);
      ZC_REQUIRE(location != zc::none);
      ZC_IF_SOME(value, location) {
        ZC_EXPECT(sourceFixture.sources->getLocOffsetInBuffer(value, sourceFixture.buffer) ==
                  span.byteStart());
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

ZC_TEST("CanonicalParsedSource.RejectsInvalidSourceRanges") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("module root;"_zcb, "main.zom");
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source(), zc::heapArray(sources.getEntireTextForBuffer(buffer)));
  const uint8_t foreign[] = {0, 1};
  const auto badRange = source::SourceRange(foreign, foreign + 1);
  ZC_IF_SOME(snapshotValue, snapshot) {
    auto tokens = parseTokenSnapshot(sources, buffer);
    auto result = test::canonicalParsedSource(snapshotValue, sources, buffer, zc::mv(tokens),
                                              manualModuleTree(badRange, "root"_zc));
    ZC_EXPECT(result == zc::none);
  }
}

ZC_TEST("ParsedModule.RejectsStaleQuerySource") {
  ParsedSource sourceFixture("module root;"_zc);
  auto snapshot = sourceFixture.snapshot();
  ZC_REQUIRE(sourceFixture.tokens != zc::none);
  auto retainedTokens = zc::mv(ZC_ASSERT_NONNULL(sourceFixture.tokens));
  auto parsed = test::canonicalParsedSource(snapshot, *sourceFixture.sources, sourceFixture.buffer,
                                            zc::mv(retainedTokens), zc::mv(sourceFixture.tree));
  ZC_REQUIRE(parsed != zc::none);

  identity::SemanticContextFactory factory;
  const auto context = requireContext(factory);
  auto registriesValue = identity::SemanticIdentityRegistrySet::create(factory, context);
  ZC_REQUIRE(registriesValue != zc::none);
  ZC_IF_SOME(registries, registriesValue) {
    ZC_REQUIRE(registries.collectCompilationUnit(userUnit()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    auto stale =
        identity::ImmutableSourceSnapshot::from(source(), zc::heapArray("module root; "_zcb));
    ZC_IF_SOME(staleValue, stale) {
      ZC_REQUIRE(registries.collectSourceFile(zc::mv(staleValue)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    auto verified = ParsedModuleVerifier::verifyQueryResult(
        context, registries, snapshot.source(), *sourceFixture.sources, sourceFixture.buffer,
        zc::mv(ZC_ASSERT_NONNULL(parsed)));
    ZC_REQUIRE(verified.is<ParsedModuleInvariantFact>());
    ZC_EXPECT(verified.get<ParsedModuleInvariantFact>().kind ==
              ParsedModuleInvariantKind::SourceMismatch);
  }
}

ZC_TEST("BindingInput.AcceptsVerifiedDependencyFreeInventory") {
  ParsedSource sourceFixture("module root;\nfun run() {}\nexport {run as execute};\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto result = verify(fixture);
  ZC_REQUIRE(result.is<VerifiedBindingInput>());
  const auto& input = result.get<VerifiedBindingInput>();
  ZC_EXPECT(input.definitions().definitions().size() == 1);
  ZC_REQUIRE(input.localExportSpecifiers().size() == 1);
  const auto& specifier = input.localExportSpecifiers()[0];
  ZC_EXPECT(specifier.sourceName().text() == "run"_zc);
  ZC_EXPECT(specifier.exportedName().text() == "execute"_zc);
  ZC_EXPECT(specifier.aliasSpan() != zc::none);
  ZC_IF_SOME(aliasSpan, specifier.aliasSpan()) {
    ZC_EXPECT(specifier.sourceNameSpan().byteStart() < aliasSpan.byteStart());
  }
  ZC_EXPECT(specifier.declarationSpan().byteStart() >= specifier.exportSpan().byteStart());
}

ZC_TEST("BindingInput.PublishesRequesterFilteredDependencyViewsAndResolvedImports") {
  ParsedSource requesterSource("module app;\nimport dep::{exported as local};\n"_zc);
  ParsedSource dependencySource(
      "module dep;\n"
      "export fun exported() {}\n"
      "fun hidden() {}\n"_zc);
  DependencySurfaceFixture fixture(requesterSource, dependencySource);
  auto result = fixture.verifyRequester();
  ZC_REQUIRE(result.is<VerifiedBindingInput>());
  const auto& input = result.get<VerifiedBindingInput>();
  ZC_REQUIRE(input.dependencySurfaces().size() == 1);
  const auto& view = input.dependencySurfaces()[0];
  ZC_EXPECT(view.requester() == fixture.requesterModule);
  ZC_EXPECT(view.sourceModule() == fixture.dependencyModule);
  ZC_REQUIRE(view.visibleEntries().size() == 1);
  ZC_EXPECT(view.visibleEntries()[0].name.name().text() == "exported"_zc);
  ZC_EXPECT(view.visibleEntries()[0].exported);
  ZC_EXPECT(input.preludeSurface() == zc::none);
  ZC_EXPECT(input.resolvedModuleAliases().size() == 0);
  ZC_REQUIRE(input.resolvedImports().size() == 1);
  const auto& import = input.resolvedImports()[0];
  ZC_EXPECT(import.requester() == fixture.requesterModule);
  ZC_EXPECT(import.sourceModule() == fixture.dependencyModule);
  ZC_EXPECT(import.kind() == ImportBindingKind::Import);
  ZC_EXPECT(import.schemaPreorderOrdinal() > 0);
  ZC_REQUIRE(import.requestedName() != zc::none);
  ZC_IF_SOME(name, import.requestedName()) {
    ZC_EXPECT(name.nameSpace() == Namespace::Value);
    ZC_EXPECT(name.name().text() == "exported"_zc);
  }
  ZC_EXPECT(import.localName().nameSpace() == Namespace::Value);
  ZC_EXPECT(import.localName().name().text() == "local"_zc);
  const auto requesterKey = moduleNamed("app"_zc);
  const auto actualRequester = import.binding().requester().encode();
  const auto expectedRequester = requesterKey.encode();
  ZC_EXPECT(sameEncodedBytes(actualRequester.asPtr(), expectedRequester.asPtr()));
  ZC_EXPECT(import.canonicalTarget().value().is<DefinitionBindingTarget>());
  ZC_EXPECT(import.aliasSpan() != zc::none);
  ZC_EXPECT(import.exportSpan() == zc::none);
  ZC_EXPECT(import.sourceReexportChain().size() == 0);
  ZC_EXPECT(import.canonicalDeclarationSpan().belongsTo(sourceNamed("dep.zom"_zc)));
  ZC_EXPECT(import.sourceRevision().digest() == view.sourceRevision().digest());
}

ZC_TEST("BindingBuilder.ResolvesSelectedImportsBeforeBodyTraversal") {
  ParsedSource requesterSource(
      "module app;\n"
      "import dep::{exported as local};\n"
      "fun use() { local; }\n"_zc);
  ParsedSource dependencySource("module dep;\nexport fun exported() {}\n"_zc);
  DependencySurfaceFixture fixture(requesterSource, dependencySource);
  auto inputResult = fixture.verifyRequester();
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto references = identifierExpressions(input.tree(), "local"_zc);
  ZC_REQUIRE(references.size() == 1);

  auto candidate = BindingBuilder::build(input, *requesterSource.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  ZC_REQUIRE(metadata.imports().size() == 1);
  ZC_EXPECT(metadata.moduleAliases().size() == 0);
  ZC_EXPECT(metadata.localExports().size() == 0);
  const auto& resolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  const auto& bound = resolution.value.get<BoundNameResolution>();
  ZC_EXPECT(bound.origin == BindingOrigin::ImportAlias);
  ZC_EXPECT(bound.nameSpace == Namespace::Value);
  ZC_REQUIRE(bound.bindingIdentity.value().is<SemanticImportBindingTarget>());
  ZC_EXPECT(bound.bindingIdentity.value().get<SemanticImportBindingTarget>().binding ==
            metadata.imports()[0].binding);
  ZC_EXPECT(requireDefinitionTarget(bound.canonicalTarget) ==
            requireDefinitionTarget(metadata.imports()[0].canonicalTarget));
  for (const auto& entry : verified.get<VerifiedBindingOutput>().surface.visibleEntries()) {
    ZC_EXPECT(entry.name.name().text() != "local"_zc);
  }
  ZC_EXPECT(requesterSource.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingBuilder.PublishesForeignAndLocalReexportsWithCanonicalProvenance") {
  ParsedSource requesterSource(
      "module app;\n"
      "export dep::{exported};\n"
      "export {exported as public_exported};\n"_zc);
  ParsedSource dependencySource("module dep;\nexport fun exported() {}\n"_zc);
  DependencySurfaceFixture fixture(requesterSource, dependencySource);
  auto inputResult = fixture.verifyRequester();
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());

  auto candidate = BindingBuilder::build(input, *requesterSource.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& output = verified.get<VerifiedBindingOutput>();
  ZC_REQUIRE(output.metadata.imports().size() == 1);
  ZC_REQUIRE(output.metadata.localExports().size() == 1);
  ZC_EXPECT(output.metadata.imports()[0].kind == ImportBindingKind::ForeignReexport);
  ZC_REQUIRE(output.metadata.imports()[0].reexportChain.size() == 1);
  ZC_REQUIRE(output.metadata.localExports()[0].reexportChain.size() == 2);
  ZC_EXPECT(requireDefinitionTarget(output.metadata.imports()[0].canonicalTarget) ==
            requireDefinitionTarget(output.metadata.localExports()[0].canonicalTarget));
  ZC_REQUIRE(output.surface.exports().size() == 2);
  ZC_EXPECT(output.surface.exports()[0].name.name().text() == "exported"_zc);
  ZC_EXPECT(output.surface.exports()[1].name.name().text() == "public_exported"_zc);
  ZC_EXPECT(requesterSource.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingVerifier.RejectsMutatedAndMissingImportFacts") {
  ParsedSource requesterSource(
      "module app;\n"
      "import dep::{exported as local};\n"
      "fun use() { local; }\n"_zc);
  ParsedSource dependencySource("module dep;\nexport fun exported() {}\n"_zc);
  DependencySurfaceFixture fixture(requesterSource, dependencySource);
  auto inputResult = fixture.verifyRequester();
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *requesterSource.diagnostics);
    ZC_REQUIRE(result.is<BindingMetadataCandidate>());
    return zc::mv(result.get<BindingMetadataCandidate>());
  };

  auto mutated = buildCandidate();
  ZC_REQUIRE(mutated.imports.size() == 1);
  mutated.imports[0].kind = ImportBindingKind::ForeignReexport;
  auto mutatedResult = BindingVerifier::verify(input, zc::mv(mutated));
  ZC_EXPECT(requireBinderInvariant(mutatedResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto missing = buildCandidate();
  missing.imports.removeLast();
  auto missingResult = BindingDifferentialOracle::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);
  ZC_EXPECT(requesterSource.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingInput.RejectsMissingAndInvisibleImportMembersWithoutPartialPublication") {
  ParsedSource requesterSource("module app;\nimport dep::{hidden, missing};\n"_zc);
  ParsedSource dependencySource(
      "module dep;\n"
      "export fun exported() {}\n"
      "fun hidden() {}\n"_zc);
  DependencySurfaceFixture fixture(requesterSource, dependencySource);
  auto result = fixture.verifyRequester();
  ZC_REQUIRE(result.is<BindingInputSourceRejected>());
  const auto failures = result.get<BindingInputSourceRejected>().failures();
  ZC_REQUIRE(failures.size() == 2);
  ZC_EXPECT(failures[0].diagnostic() == BindingInputDiagnostic::ImportTargetNotVisible);
  ZC_EXPECT(failures[1].diagnostic() == BindingInputDiagnostic::ImportMemberNotFound);
  ZC_EXPECT(failures[0].source().byteStart() < failures[1].source().byteStart());
  auto consumer = zc::heap<ModuleGraphDiagnosticCapture>();
  const auto& capture = *consumer;
  requesterSource.diagnostics->addConsumer(zc::mv(consumer));
  ZC_IF_SOME(parsed, fixture.requesterParsed) {
    ZC_EXPECT(canEmitBindingInputSourceFailure(parsed, failures[0]));
    ZC_EXPECT(emitBindingInputSourceFailure(*requesterSource.diagnostics, parsed, failures[0]));
    ZC_EXPECT(capture.id == diagnostics::DiagID::ImportTargetNotVisible);
    ZC_EXPECT(capture.rendered == "Import target is not visible from this module"_zc);
    ZC_EXPECT(emitBindingInputSourceFailure(*requesterSource.diagnostics, parsed, failures[1]));
    ZC_EXPECT(capture.id == diagnostics::DiagID::ImportMemberNotFound);
    ZC_EXPECT(capture.rendered == "Module 'dep' has no exported member 'missing'"_zc);
    ZC_EXPECT(capture.count == 2);
    ZC_EXPECT(capture.ranges.size() == 2);
  }
  ParsedSource foreignSource("module root;\n"_zc);
  auto foreignParsed = alternateVerifiedParsedModule(foreignSource);
  ZC_EXPECT(!canEmitBindingInputSourceFailure(foreignParsed, failures[0]));
  ZC_EXPECT(
      !emitBindingInputSourceFailure(*requesterSource.diagnostics, foreignParsed, failures[0]));
  ZC_EXPECT(capture.count == 2);
}

ZC_TEST("BindingInput.PublishesForeignReexportsAndModuleAliasesFromExactGraphTargets") {
  ParsedSource reexportSource("module app;\nexport dep::{exported};\n"_zc);
  ParsedSource dependencySource("module dep;\nexport fun exported() {}\n"_zc);
  DependencySurfaceFixture reexportFixture(reexportSource, dependencySource);
  auto reexportResult = reexportFixture.verifyRequester();
  ZC_REQUIRE(reexportResult.is<VerifiedBindingInput>());
  const auto& reexportInput = reexportResult.get<VerifiedBindingInput>();
  ZC_REQUIRE(reexportInput.resolvedImports().size() == 1);
  ZC_EXPECT(reexportInput.resolvedImports()[0].kind() == ImportBindingKind::ForeignReexport);
  ZC_EXPECT(reexportInput.resolvedImports()[0].exportSpan() != zc::none);

  ParsedSource aliasSource("export module dep_alias = dep::core;\n"_zc);
  ParsedSource aliasDependencySource("module dep;\nexport fun exported() {}\n"_zc);
  DependencySurfaceFixture aliasFixture(aliasSource, aliasDependencySource, true);
  auto aliasResult = aliasFixture.verifyRequester();
  ZC_REQUIRE(aliasResult.is<VerifiedBindingInput>());
  auto aliasInput = zc::mv(aliasResult.get<VerifiedBindingInput>());
  ZC_REQUIRE(aliasInput.resolvedModuleAliases().size() == 1);
  const auto& alias = aliasInput.resolvedModuleAliases()[0];
  ZC_EXPECT(alias.requester() == aliasFixture.requesterModule);
  ZC_EXPECT(alias.targetModule() == aliasFixture.dependencyModule);
  ZC_EXPECT(alias.localName().nameSpace() == Namespace::Module);
  ZC_EXPECT(alias.localName().name().text() == "dep_alias"_zc);
  ZC_EXPECT(alias.schemaPreorderOrdinal() > 0);
  ZC_EXPECT(alias.exported());
  ZC_EXPECT(alias.declarationSpan().belongsTo(sourceNamed("app.zom"_zc)));
  ZC_EXPECT(alias.targetSpan().belongsTo(sourceNamed("app.zom"_zc)));
  auto aliasCandidate = BindingBuilder::build(aliasInput, *aliasSource.diagnostics);
  ZC_REQUIRE(aliasCandidate.is<BindingMetadataCandidate>());
  auto aliasOutput =
      BindingVerifier::verify(aliasInput, zc::mv(aliasCandidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(aliasOutput.is<VerifiedBindingOutput>());
  ZC_REQUIRE(aliasOutput.get<VerifiedBindingOutput>().metadata.moduleAliases().size() == 1);
  ZC_REQUIRE(aliasOutput.get<VerifiedBindingOutput>().surface.exports().size() == 1);
  ZC_EXPECT(aliasOutput.get<VerifiedBindingOutput>().surface.exports()[0].name.name().text() ==
            "dep_alias"_zc);

  ParsedSource rejectedSource("module app;\nexport dep::{hidden, missing};\n"_zc);
  ParsedSource rejectedDependency(
      "module dep;\n"
      "fun hidden() {}\n"_zc);
  DependencySurfaceFixture rejectedFixture(rejectedSource, rejectedDependency);
  auto rejectedResult = rejectedFixture.verifyRequester();
  ZC_REQUIRE(rejectedResult.is<BindingInputSourceRejected>());
  const auto failures = rejectedResult.get<BindingInputSourceRejected>().failures();
  ZC_REQUIRE(failures.size() == 2);
  ZC_EXPECT(failures[0].diagnostic() == BindingInputDiagnostic::ReexportTargetNotVisible);
  ZC_EXPECT(failures[1].diagnostic() == BindingInputDiagnostic::ReexportMemberNotFound);
  auto consumer = zc::heap<ModuleGraphDiagnosticCapture>();
  const auto& capture = *consumer;
  rejectedSource.diagnostics->addConsumer(zc::mv(consumer));
  ZC_IF_SOME(parsed, rejectedFixture.requesterParsed) {
    ZC_EXPECT(emitBindingInputSourceFailure(*rejectedSource.diagnostics, parsed, failures[0]));
    ZC_EXPECT(capture.id == diagnostics::DiagID::ReexportTargetNotVisible);
    ZC_EXPECT(capture.rendered == "Re-export target is not visible from this module"_zc);
  }
}

ZC_TEST("BindingInput.RejectsIncompleteAdditionalAndCrossContextDependencySurfaces") {
  ParsedSource requesterSource("module app;\nimport dep::{exported};\n"_zc);
  ParsedSource dependencySource("module dep;\nexport fun exported() {}\n"_zc);
  DependencySurfaceFixture fixture(requesterSource, dependencySource);

  auto missing = fixture.verifyRequesterWith(zc::ArrayPtr<const DependencyExportSurface>());
  ZC_REQUIRE(missing.is<ModuleGraphInvariantFact>());
  ZC_EXPECT(missing.get<ModuleGraphInvariantFact>().kind ==
            ModuleGraphInvariantKind::IncompleteResolution);

  ZC_REQUIRE(fixture.dependencySurface != zc::none);
  ZC_IF_SOME(surface, fixture.dependencySurface) {
    DependencyExportSurface additional[] = {{fixture.dependencyModule, surface},
                                            {fixture.dependencyModule, surface}};
    auto additionalResult = fixture.verifyRequesterWith(additional);
    ZC_REQUIRE(additionalResult.is<ModuleGraphInvariantFact>());
    ZC_EXPECT(additionalResult.get<ModuleGraphInvariantFact>().kind ==
              ModuleGraphInvariantKind::IncompleteResolution);
  }

  ParsedSource foreignRequesterSource("module app;\nimport dep::{exported};\n"_zc);
  ParsedSource foreignDependencySource("module dep;\nexport fun exported() {}\n"_zc);
  DependencySurfaceFixture foreign(foreignRequesterSource, foreignDependencySource);
  ZC_REQUIRE(foreign.dependencySurface != zc::none);
  ZC_IF_SOME(surface, foreign.dependencySurface) {
    DependencyExportSurface crossContext[] = {{foreign.dependencyModule, surface}};
    auto crossContextResult = fixture.verifyRequesterWith(crossContext);
    ZC_REQUIRE(crossContextResult.is<ModuleGraphInvariantFact>());
    ZC_EXPECT(crossContextResult.get<ModuleGraphInvariantFact>().kind ==
              ModuleGraphInvariantKind::InputMismatch);
  }
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
              "c14457c2e7687474842773a1e68d30e1054679567dbe340ea9d8f5125c4b7c19"_zc);
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
  ZC_EXPECT(dump.size() == 324);
  ZC_IF_SOME(digest, identity::sha256(dump.asPtr())) {
    ZC_EXPECT(zc::encodeHex(digest.bytes()) ==
              "f51085fd79a7078bdee37c5dc38c547e37041daac0a2a8590949b2971ac61202"_zc);
    return;
  }
  ZC_EXPECT(false);
}

ZC_TEST("BindingExtensionFraming.MatchesNormativeRFC0014Oracles") {
  const uint8_t selfRecord[] = {0xa1};
  const uint8_t firstThisRecord[] = {0xb2};
  const uint8_t secondThisRecord[] = {0xc3};
  zc::Vector<zc::ArrayPtr<const uint8_t>> selfTypes;
  zc::Vector<zc::ArrayPtr<const uint8_t>> thisBindings;

  const auto verifyOracle = [&](zc::StringPtr expectedPreimage, zc::StringPtr expectedDigest) {
    const auto framed = frameBindingExtensionSequences(selfTypes.asPtr(), thisBindings.asPtr());
    const auto preimage = decodeHexBytes(expectedPreimage);
    ZC_EXPECT(framed.asPtr() == preimage.asPtr());
    ZC_IF_SOME(digest, identity::sha256(framed.asPtr())) {
      ZC_EXPECT(zc::encodeHex(digest.bytes()) == expectedDigest);
      return;
    }
    ZC_EXPECT(false);
  };

  verifyOracle("00000000000000000000000000000000"_zc,
               "374708fff7719dd5979ec875d56cd2286f6d3cf7ec317a3b25632aab28ec37bb"_zc);
  selfTypes.add(zc::arrayPtr(selfRecord));
  verifyOracle("0000000000000001a10000000000000000"_zc,
               "c02ad86008dffaa50c141da2dc596ec90bf4fdddb44e29500d33ba32ab9a07eb"_zc);
  thisBindings.add(zc::arrayPtr(firstThisRecord));
  thisBindings.add(zc::arrayPtr(secondThisRecord));
  verifyOracle("0000000000000001a10000000000000002b2c3"_zc,
               "c8d7b4d650f7730b37fe8f562b8963fa19c339fc3f9d01528090d49d1c1aea22"_zc);
}

ZC_TEST("BindingExtensionFraming.ComposesCompleteContextualSelfRecords") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Box { fun use(this: Self) -> Self { this; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidateResult = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidateResult.is<BindingMetadataCandidate>());
  const auto& candidate = candidateResult.get<BindingMetadataCandidate>();
  ZC_REQUIRE(candidate.selfTypes.size() == 2);
  ZC_REQUIRE(candidate.thisBindings.size() == 1);

  const auto encode =
      [&](zc::ArrayPtr<const BoundSelfType> selfTypes,
          zc::ArrayPtr<const BoundThis> thisBindings) -> zc::Maybe<zc::Array<uint8_t>> {
    identity::CanonicalEncoder encoder;
    if (!encodeBindingExtensionSequences(encoder, input, selfTypes, thisBindings)) {
      return zc::none;
    }
    return encoder.finish();
  };
  auto complete = encode(candidate.selfTypes.asPtr(), candidate.thisBindings.asPtr());
  auto selfOnly = encode(candidate.selfTypes.asPtr(), zc::ArrayPtr<const BoundThis>());
  auto thisOnly = encode(zc::ArrayPtr<const BoundSelfType>(), candidate.thisBindings.asPtr());
  ZC_REQUIRE(complete != zc::none);
  ZC_REQUIRE(selfOnly != zc::none);
  ZC_REQUIRE(thisOnly != zc::none);
  const auto& completeBytes = ZC_ASSERT_NONNULL(complete);
  const auto& selfBytes = ZC_ASSERT_NONNULL(selfOnly);
  const auto& thisBytes = ZC_ASSERT_NONNULL(thisOnly);
  ZC_REQUIRE(selfBytes.size() >= 16);
  ZC_REQUIRE(thisBytes.size() >= 16);
  const auto zeroCount = decodeHexBytes("0000000000000000"_zc);
  ZC_EXPECT(selfBytes.asPtr().slice(selfBytes.size() - 8) == zeroCount.asPtr());
  ZC_EXPECT(thisBytes.asPtr().first(8) == zeroCount.asPtr());
  zc::Vector<uint8_t> composed;
  composed.addAll(selfBytes.asPtr().slice(0, selfBytes.size() - 8));
  composed.addAll(thisBytes.asPtr().slice(8));
  ZC_EXPECT(completeBytes.asPtr() == composed.asPtr());
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
            "33ff40332a1ec68069d374a6b3a556080758aa9d2ee7fbbe28c779185f32ad12"_zc);
  ZC_IF_SOME(dump, encodeBindingAllocationDump(input, output.metadata.scopes(),
                                               output.metadata.labels())) {
    ZC_EXPECT(dump.size() == 1433);
    ZC_IF_SOME(digest, identity::sha256(dump.asPtr())) {
      ZC_EXPECT(zc::encodeHex(digest.bytes()) ==
                "ca2482fac415523141aea9d8f8876a2e72f215680f0bfc5182e5e1d1b77b305e"_zc);
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
  ZC_REQUIRE(closureFirst.owner.value().is<AnonymousLabelOwner>());
  ZC_REQUIRE(closureSecond.owner.value().is<AnonymousLabelOwner>());
  const auto functionExpressions = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  ZC_REQUIRE(functionExpressions.size() == 1);
  const auto& closure = requireAnonymousAt(input, functionExpressions[0]);
  ZC_EXPECT(closureFirst.owner.value().get<AnonymousLabelOwner>().anonymous == closure);
  ZC_EXPECT(closureSecond.owner.value().get<AnonymousLabelOwner>().anonymous == closure);
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
                "a7b2e6d18b759ccbb20045587c05dc158c6dde2385763befb7c69f92f803c0cc"_zc);
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
  ZC_EXPECT(requireBinderInvariant(extraResult).kind == BinderInvariantKind::InvalidEmitterOrdinal);

  auto bindingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(bindingCandidate.is<BindingMetadataCandidate>());
  auto& declarationBinding = bindingCandidate.get<BindingMetadataCandidate>();
  declarationBinding.nodeBindings.add(
      BindingResolution{labeledNodes[1], BindingResolutionValue(FailedBindingResolution{0})});
  auto bindingResult = BindingVerifier::verify(input, zc::mv(declarationBinding));
  ZC_EXPECT(requireBinderInvariant(bindingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);
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
  ZC_EXPECT(
      sourceFixture.sources->getLocOffsetInBuffer(capture.locations[0], sourceFixture.buffer) ==
      value.sourceFailures[0].primary.byteStart());
  ZC_EXPECT(
      sourceFixture.sources->getLocOffsetInBuffer(capture.locations[1], sourceFixture.buffer) ==
      value.sourceFailures[1].primary.byteStart());

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

ZC_TEST("BindingVerifier.RejectsIncompleteCurrentSurface") {
  ParsedSource sourceFixture("module root;\nfun first();\nfun second();\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.currentSurface.visibleEntries.size() == 2);
  value.currentSurface.visibleEntries.removeLast();

  auto result = BindingVerifier::verify(input, zc::mv(value));
  const auto& fact = requireBinderInvariant(result);
  ZC_EXPECT(fact.kind == BinderInvariantKind::MissingRequiredResolution);
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
  surface.sourceCompilationUnit = foreignInput.compilationUnit();
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

ZC_TEST("BindingRun.PublishesOnlyVerifiedFactsOrClosedRejection") {
  ParsedSource validSource("module root;\nfun run() { let value = 1; value; }\n"_zc);
  FrozenFixture validFixture(validSource, true);
  auto validInputResult = verify(validFixture);
  ZC_REQUIRE(validInputResult.is<VerifiedBindingInput>());
  auto validInput = zc::mv(validInputResult.get<VerifiedBindingInput>());
  auto verified = runBinding(validInput, *validSource.diagnostics);
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  ZC_EXPECT(verified.get<VerifiedBindingOutput>().metadata.module() == validInput.module());

  ParsedSource invalidSource("module root;\nfun run() { missing; }\n"_zc);
  FrozenFixture invalidFixture(invalidSource, true);
  auto invalidInputResult = verify(invalidFixture);
  ZC_REQUIRE(invalidInputResult.is<VerifiedBindingInput>());
  auto invalidInput = zc::mv(invalidInputResult.get<VerifiedBindingInput>());
  auto rejected = runBinding(invalidInput, *invalidSource.diagnostics);
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(rejected.get<SourceRejected>().failures().size() == 1);
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

ZC_TEST("StableIdentityPreAdmission.RejectsDuplicateFunctionsBeforeRegistryMutation") {
  ParsedSource sourceFixture("module root;\nfun value();\nfun value();\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<VerifiedStableIdentityCandidateInventory>());
  auto inventory = zc::mv(verification.get<VerifiedStableIdentityCandidateInventory>());
  auto validation =
      StableIdentityCandidateVerifier::findDefinitionRedeclarations(inventory.definitions.asPtr());
  ZC_REQUIRE(validation.is<zc::Vector<StableDefinitionRedeclaration>>());
  auto redeclarations = zc::mv(validation.get<zc::Vector<StableDefinitionRedeclaration>>());
  ZC_REQUIRE(inventory.definitions.size() == 2);
  ZC_REQUIRE(redeclarations.size() == 1);
  ZC_EXPECT(redeclarations[0].first == 0);
  ZC_EXPECT(redeclarations[0].duplicate == 1);
  ZC_EXPECT(redeclarations[0].diagnostic == BinderDiagnosticCode::RedeclareFunction);
  emitStableRedeclarations(sourceFixture, inventory.definitions.asPtr(), redeclarations.asPtr());
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("StableIdentityPreAdmission.RejectsNfcEquivalentFunctionNames") {
  ParsedSource sourceFixture("module root;\nfun e\xcc\x81();\nfun \xc3\xa9();\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<VerifiedStableIdentityCandidateInventory>());
  auto inventory = zc::mv(verification.get<VerifiedStableIdentityCandidateInventory>());
  auto validation =
      StableIdentityCandidateVerifier::findDefinitionRedeclarations(inventory.definitions.asPtr());
  ZC_REQUIRE(validation.is<zc::Vector<StableDefinitionRedeclaration>>());
  auto redeclarations = zc::mv(validation.get<zc::Vector<StableDefinitionRedeclaration>>());
  ZC_REQUIRE(redeclarations.size() == 1);
  ZC_EXPECT(redeclarations[0].diagnostic == BinderDiagnosticCode::RedeclareFunction);
  ZC_EXPECT(inventory.definitions[redeclarations[0].first].authority.key() ==
            inventory.definitions[redeclarations[0].duplicate].authority.key());
  emitStableRedeclarations(sourceFixture, inventory.definitions.asPtr(), redeclarations.asPtr());
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("StableIdentityPreAdmission.UsesKindSpecificRedeclarationCodes") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun f(); fun f();\n"
      "class C {} class C {}\n"
      "interface I {} interface I {}\n"
      "enum E {} enum E {}\n"
      "alias A = i32; alias A = i32;\n"
      "struct S {} struct S {}\n"
      "class Holder { let value: i32; let value: i32; }\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<VerifiedStableIdentityCandidateInventory>());
  auto inventory = zc::mv(verification.get<VerifiedStableIdentityCandidateInventory>());
  auto validation =
      StableIdentityCandidateVerifier::findDefinitionRedeclarations(inventory.definitions.asPtr());
  ZC_REQUIRE(validation.is<zc::Vector<StableDefinitionRedeclaration>>());
  auto redeclarations = zc::mv(validation.get<zc::Vector<StableDefinitionRedeclaration>>());
  const BinderDiagnosticCode expected[] = {
      BinderDiagnosticCode::RedeclareFunction,  BinderDiagnosticCode::RedeclareClass,
      BinderDiagnosticCode::RedeclareInterface, BinderDiagnosticCode::RedeclareEnum,
      BinderDiagnosticCode::RedeclareTypeAlias, BinderDiagnosticCode::DuplicateIdentifier,
      BinderDiagnosticCode::RedeclareVariable,
  };
  ZC_REQUIRE(redeclarations.size() == zc::size(expected));
  for (size_t index = 0; index < zc::size(expected); ++index) {
    ZC_EXPECT(redeclarations[index].diagnostic == expected[index]);
    ZC_EXPECT(redeclarations[index].first < redeclarations[index].duplicate);
  }
  emitStableRedeclarations(sourceFixture, inventory.definitions.asPtr(), redeclarations.asPtr());
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == zc::size(expected));
}

ZC_TEST("StableIdentityPreAdmission.IndependentlyReconstructsNestedStableOwnerChains") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Box<T> { fun map(value: T) -> T; }\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<VerifiedStableIdentityCandidateInventory>());
  const auto& inventory = verification.get<VerifiedStableIdentityCandidateInventory>();
  ZC_REQUIRE(inventory.definitions.size() == 2);
  const VerifiedStableDefinitionCandidate* method = nullptr;
  for (const auto& definition : inventory.definitions) {
    if (definition.authority.record().kind() == identity::DefinitionKind::Method) {
      method = &definition;
    }
  }
  ZC_REQUIRE(method != nullptr);
  ZC_REQUIRE(method->authority.record().owners().size() == 1);
  ZC_EXPECT(method->authority.record().owners()[0].kind() ==
            identity::EnclosingStableOwnerKind::Definition);
}

ZC_TEST("StableIdentityPreAdmission.ExcludesDefinitionsBelowAnonymousOwners") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun outer() { const closure = fun() { fun hidden(); }; }\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<VerifiedStableIdentityCandidateInventory>());
  const auto& inventory = verification.get<VerifiedStableIdentityCandidateInventory>();
  ZC_REQUIRE(inventory.definitions.size() == 1);
  ZC_EXPECT(inventory.definitions[0].authority.record().kind() ==
            identity::DefinitionKind::Function);
  ZC_EXPECT(inventory.definitions[0].authority.record().owners().size() == 0);
}

ZC_TEST("StableIdentityPreAdmission.RejectsTheFirstDuplicateGenericParameter") {
  ParsedSource sourceFixture("module root;\nfun run<T, U, T, T>();\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<StableIdentityCandidateSourceFailure>());
  const auto& failure = verification.get<StableIdentityCandidateSourceFailure>();
  ZC_EXPECT(failure.kind == StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter);
  ZC_EXPECT(failure.source.byteStart() == 27);
  ZC_EXPECT(failure.source.byteEnd() == 28);
  ZC_REQUIRE(failure.previous != zc::none);
  ZC_IF_SOME(previous, failure.previous) {
    ZC_EXPECT(previous.byteStart() == 21);
    ZC_EXPECT(previous.byteEnd() == 22);
  }
  ZC_REQUIRE(failure.identifier != zc::none);
  ZC_IF_SOME(identifier, failure.identifier) { ZC_EXPECT(identifier.text() == "T"_zc); }
  emitStableSourceFailure(sourceFixture, failure);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("StableIdentityPreAdmission.RejectsNfcEquivalentGenericParameters") {
  ParsedSource sourceFixture("module root;\nfun run<e\xcc\x81, \xc3\xa9>();\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<StableIdentityCandidateSourceFailure>());
  const auto& failure = verification.get<StableIdentityCandidateSourceFailure>();
  ZC_EXPECT(failure.kind == StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter);
  ZC_EXPECT(failure.source.byteStart() == 26);
  ZC_EXPECT(failure.source.byteEnd() == 28);
  ZC_REQUIRE(failure.previous != zc::none);
  ZC_IF_SOME(previous, failure.previous) {
    ZC_EXPECT(previous.byteStart() == 21);
    ZC_EXPECT(previous.byteEnd() == 24);
  }
  ZC_REQUIRE(failure.identifier != zc::none);
  ZC_IF_SOME(identifier, failure.identifier) { ZC_EXPECT(identifier.text() == "\xc3\xa9"_zc); }
  emitStableSourceFailure(sourceFixture, failure);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("FrozenInventory.RejectsMissingAdditionalAndWrongKindDefinitions") {
  ParsedSource missingSource("module root;\nfun run() {}\n"_zc);
  FrozenFixture missingFixture(missingSource);
  ZC_REQUIRE(missingFixture.inventoryFailure != zc::none);
  ZC_IF_SOME(kind, missingFixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::IncompleteInventory);
  }

  ParsedSource additionalSource("module root;\nfun run() {}\n"_zc);
  FrozenFixture additionalFixture(additionalSource, true, false, ImplRegistration::None, true);
  ZC_REQUIRE(additionalFixture.inventoryFailure != zc::none);
  ZC_IF_SOME(kind, additionalFixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::IncompleteInventory);
  }

  ParsedSource wrongSource("module root;\nfun run() {}\n"_zc);
  FrozenFixture wrongFixture(wrongSource, true, true);
  ZC_IF_SOME(kind, wrongFixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
  } else {
    ZC_EXPECT(false);
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
      ZC_EXPECT(inventory.impls()[0].authority == fixture.implId);
      ZC_EXPECT(inventory.implAt(inventory.impls()[0].node) == inventory.impls()[0].occurrence);
    } else {
      ZC_EXPECT(false);
    }
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
    ZC_REQUIRE(inventory.implAuthorities().size() == 1);
    const auto expectedDefinition = inventory.definitions()[0].key.encode();
    const auto expectedImplementation = inventory.implAuthorities()[0].key.encode();
    const auto definition = inventory.definitions()[0].definition;
    const auto implementation = inventory.impls()[0].authority;

    auto displacedRegistries = zc::mv(fixture.registries);
    ZC_REQUIRE(displacedRegistries.definitions().lookup(definition) != zc::none);
    ZC_REQUIRE(displacedRegistries.impls().lookup(implementation) != zc::none);
    ZC_IF_SOME(key, inventory.definitionKey(definition)) {
      const auto encoded = key.encode();
      ZC_EXPECT(sameEncodedBytes(encoded.asPtr(), expectedDefinition.asPtr()));
    } else {
      ZC_EXPECT(false);
    }
    ZC_IF_SOME(key, inventory.implKey(implementation)) {
      const auto encoded = key.encode();
      ZC_EXPECT(sameEncodedBytes(encoded.asPtr(), expectedImplementation.asPtr()));
    } else {
      ZC_EXPECT(false);
    }
  }
}

ZC_TEST("FrozenInventory.ProjectsVirtualModuleBodyPathsAcrossSourceForms") {
  ParsedSource rootSource(
      "module root;\n"
      "for (let item in [1]) { let local = 1; const closure = () => local; }\n"_zc);
  ParsedSource inlineSource(
      "module root {\n"
      "  for (let item in [1]) { let local = 1; const closure = () => local; }\n"
      "}\n"_zc);
  ParsedSource implicitSource(
      "for (let item in [1]) { let local = 1; const closure = () => local; }\n"_zc);
  FrozenFixture rootFixture(rootSource, true);
  FrozenFixture inlineFixture(inlineSource, true);
  FrozenFixture implicitFixture(implicitSource, true);
  auto rootResult = verify(rootFixture);
  auto inlineResult = verify(inlineFixture);
  auto implicitResult = verify(implicitFixture);
  ZC_REQUIRE(rootResult.is<VerifiedBindingInput>());
  ZC_REQUIRE(inlineResult.is<VerifiedBindingInput>());
  ZC_REQUIRE(implicitResult.is<VerifiedBindingInput>());
  auto rootInput = zc::mv(rootResult.get<VerifiedBindingInput>());
  auto inlineInput = zc::mv(inlineResult.get<VerifiedBindingInput>());
  auto implicitInput = zc::mv(implicitResult.get<VerifiedBindingInput>());

  const auto compareBinding = [&](zc::StringPtr name, OwnerLocalBindingKind kind) {
    const auto& root = requireNamedOwnerLocalEntry(rootInput, name, kind);
    const auto& inlineRoot = requireNamedOwnerLocalEntry(inlineInput, name, kind);
    const auto& implicit = requireNamedOwnerLocalEntry(implicitInput, name, kind);
    ZC_EXPECT(root.key.owner().kind() == StableBodyOwnerKind::Module);
    ZC_EXPECT(root.key == inlineRoot.key);
    ZC_EXPECT(root.key == implicit.key);
    ZC_REQUIRE(root.key.path().components().size() != 0);
    ZC_EXPECT(root.key.path().components()[0] == 0);
  };
  compareBinding("item"_zc, OwnerLocalBindingKind::PatternBinding);
  compareBinding("local"_zc, OwnerLocalBindingKind::Local);
  compareBinding("closure"_zc, OwnerLocalBindingKind::Local);

  ZC_REQUIRE(rootInput.definitions().anonymousEntities().size() == 1);
  ZC_REQUIRE(inlineInput.definitions().anonymousEntities().size() == 1);
  ZC_REQUIRE(implicitInput.definitions().anonymousEntities().size() == 1);
  const auto& rootAnonymous = rootInput.definitions().anonymousEntities()[0].key;
  const auto& inlineAnonymous = inlineInput.definitions().anonymousEntities()[0].key;
  const auto& implicitAnonymous = implicitInput.definitions().anonymousEntities()[0].key;
  ZC_EXPECT(rootAnonymous.owner().kind() == StableBodyOwnerKind::Module);
  ZC_EXPECT(rootAnonymous == inlineAnonymous);
  ZC_EXPECT(rootAnonymous == implicitAnonymous);
  ZC_REQUIRE(rootAnonymous.path().components().size() != 0);
  ZC_EXPECT(rootAnonymous.path().components()[0] == 0);
  ZC_EXPECT(rootAnonymous.role() == AnonymousOwnerLocalRole::Closure);

  const auto& closure =
      requireNamedOwnerLocalEntry(rootInput, "closure"_zc, OwnerLocalBindingKind::Local);
  ZC_EXPECT(rootAnonymous.owner() == closure.key.owner());
  ZC_REQUIRE(closure.key.path().components().size() != 0);
  ZC_EXPECT(rootAnonymous.path().components()[0] == closure.key.path().components()[0]);
}

ZC_TEST("FrozenInventory.RejectsMissingImplIdentity") {
  ParsedSource sourceFixture("module root;\nimpl Trait for Target {}\n"_zc);
  FrozenFixture fixture(sourceFixture);
  ZC_IF_SOME(kind, fixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::IncompleteInventory);
  } else {
    ZC_EXPECT(false);
  }
}

ZC_TEST("FrozenInventory.RejectsWrongImplIdentity") {
  ParsedSource sourceFixture("module root;\nimpl Trait for Target {}\n"_zc);
  FrozenFixture fixture(sourceFixture, false, false, ImplRegistration::WrongOrdinal);
  ZC_IF_SOME(kind, fixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
  } else {
    ZC_EXPECT(false);
  }
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

ZC_TEST("BindingSkeleton.RetainsClosedMemberVisibilityFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Box { fun hidden(); public fun open(); protected let hook: i32; }\n"
      "interface Trait { fun required(); private fun helper(); }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto facts = verified.get<VerifiedBindingOutput>().metadata.definitions();
  const auto hidden =
      requireNamedFrozenDefinition(input, "hidden"_zc, identity::DefinitionKind::Method);
  const auto open =
      requireNamedFrozenDefinition(input, "open"_zc, identity::DefinitionKind::Method);
  const auto hook = requireNamedFrozenDefinition(input, "hook"_zc, identity::DefinitionKind::Field);
  const auto required =
      requireNamedFrozenDefinition(input, "required"_zc, identity::DefinitionKind::Method);
  const auto helper =
      requireNamedFrozenDefinition(input, "helper"_zc, identity::DefinitionKind::Method);
  ZC_EXPECT(requireMemberVisibility(requireDefinitionFact(facts, hidden)) ==
            MemberVisibility::Private);
  ZC_EXPECT(requireMemberVisibility(requireDefinitionFact(facts, open)) ==
            MemberVisibility::Public);
  ZC_EXPECT(requireMemberVisibility(requireDefinitionFact(facts, hook)) ==
            MemberVisibility::Protected);
  ZC_EXPECT(requireMemberVisibility(requireDefinitionFact(facts, required)) ==
            MemberVisibility::Public);
  ZC_EXPECT(requireMemberVisibility(requireDefinitionFact(facts, helper)) ==
            MemberVisibility::Private);

  auto semanticMutation = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(semanticMutation.is<BindingMetadataCandidate>());
  for (auto& fact : semanticMutation.get<BindingMetadataCandidate>().definitions) {
    if (fact.identity == hidden) { fact.memberVisibility = MemberVisibility::Public; }
  }
  auto semanticResult = BindingDifferentialOracle::verify(
      input, zc::mv(semanticMutation.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireBinderInvariant(semanticResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto structuralMutation = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(structuralMutation.is<BindingMetadataCandidate>());
  for (auto& fact : structuralMutation.get<BindingMetadataCandidate>().definitions) {
    if (fact.identity == hidden) { fact.memberVisibility = zc::none; }
  }
  auto structuralResult =
      BindingVerifier::verify(input, zc::mv(structuralMutation.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireBinderInvariant(structuralResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
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
  ZC_EXPECT(implFacts[0].authority == implFixture.implId);
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
  const auto& parameterFacts =
      parameterResult.get<DefinitionSkeletonCandidate>().callableParameters;
  ZC_REQUIRE(parameterFacts.size() == 1);
  const auto& parameterFact = parameterFacts[0];
  ZC_REQUIRE(parameterFact.name != zc::none);
  ZC_IF_SOME(name, parameterFact.name) { ZC_EXPECT(name.text() == "value"_zc); }
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
  ZC_EXPECT(facts[0].authority == fixture.implId);
  ZC_EXPECT(facts[0].members.empty());
}

ZC_TEST("BodyBinding.ResolvesShortAndCurrentModuleQualifiedMarkerImplPaths") {
  for (const auto sourceText : {
           "module root;\ninterface Shared {}\nclass Target {}\nimpl Shared for Target;\n"_zc,
           "module root;\ninterface Shared {}\nclass Target {}\nimpl root::Shared for Target;\n"_zc,
       }) {
    ParsedSource sourceFixture(sourceText);
    FrozenFixture fixture(sourceFixture, true, false, ImplRegistration::Exact);
    auto inputResult = verify(fixture);
    ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
    auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
    auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
    ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
    auto& value = candidate.get<BindingMetadataCandidate>();
    ZC_REQUIRE(value.sourceFailures.empty());
    const auto markerPaths = nodesOfKind(input.tree(), ast::SyntaxKind::AttributePath);
    ZC_REQUIRE(markerPaths.size() == 1);
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), markerPaths[0]);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    const auto& bound = resolution.value.get<BoundNameResolution>();
    ZC_EXPECT(bound.nameSpace == Namespace::Type);
    ZC_EXPECT(
        requireDefinitionTarget(bound.bindingIdentity) ==
        requireScopeDefinitionInNamespace(value.scopes.asPtr(), "Shared"_zc, Namespace::Type));
    auto verified = BindingVerifier::verify(input, zc::mv(value));
    ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  }
}

ZC_TEST("BodyBinding.RejectsUnverifiedForeignQualifiedMarkerImplPaths") {
  ParsedSource sourceFixture(
      "module root;\ninterface Shared {}\nclass Target {}\nimpl other::Shared for Target;\n"_zc);
  FrozenFixture fixture(sourceFixture, true, false, ImplRegistration::Exact);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BinderInvariantFact>());
  ZC_EXPECT(candidate.get<BinderInvariantFact>().kind ==
            BinderInvariantKind::MissingRequiredResolution);
  ZC_EXPECT(candidate.get<BinderInvariantFact>().emitterSite == BinderEmitterSite::BodyBinding);
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
  for (const auto& fact : metadata.genericParameters()) {
    ++genericCount;
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
  for (const auto& fact : metadata.callableParameters()) {
    ++parameterCount;
    ZC_REQUIRE(fact.name != zc::none);
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
      "  deinit() {}\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  size_t specialIdentityCount = 0;
  for (const auto& entry : input.definitions().definitions()) {
    if (entry.record.kind() != identity::DefinitionKind::Constructor &&
        entry.record.kind() != identity::DefinitionKind::Destructor) {
      continue;
    }
    ++specialIdentityCount;
    ZC_REQUIRE(entry.bindingName != zc::none);
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
  }
  for (const auto& fact : metadata.callableParameters()) {
    ++parameterCount;
    ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Function);
  }
  for (const auto& scope : metadata.scopes()) {
    if (scope.kind != ScopeKind::Function) { continue; }
    ++specialCallableScopeCount;
  }
  ZC_EXPECT(constructorCount == 1);
  ZC_EXPECT(destructorCount == 1);
  ZC_EXPECT(parameterCount == 1);
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

  const auto closureCount = input.definitions().anonymousEntities().size();
  size_t genericCount = 0;
  size_t parameterCount = 0;
  for (const auto& fact : metadata.ownerLocalBindings()) {
    if (fact.kind == OwnerLocalBindingKind::GenericParameter) {
      ++genericCount;
    } else if (fact.kind == OwnerLocalBindingKind::CallableParameter) {
      ++parameterCount;
    } else {
      continue;
    }
    ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Closure);
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
  const auto& direct = requireAnonymousAt(input, functionExpressions[0]);
  const auto& empty = requireAnonymousAt(input, lambdas[0]);
  const auto& fromLoop = requireAnonymousAt(input, lambdas[1]);
  const auto& fromMatch = requireAnonymousAt(input, lambdas[2]);
  auto parameter =
      requireNamedFrozenBindingTarget(input, "parameter"_zc, identity::DefinitionKind::Parameter);
  auto local = requireNamedFrozenBindingTarget(input, "local"_zc, identity::DefinitionKind::Local);
  auto own = requireNamedFrozenBindingTarget(input, "own"_zc, identity::DefinitionKind::Parameter);
  auto closureLocal =
      requireNamedFrozenBindingTarget(input, "closureLocal"_zc, identity::DefinitionKind::Local);
  auto item =
      requireNamedFrozenBindingTarget(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  auto matched = requireNamedFrozenBindingTarget(input, "matched"_zc,
                                                 identity::DefinitionKind::PatternBinding);

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
    ZC_EXPECT(!sameBindingTargetForTest(variable.target, own));
    ZC_EXPECT(!sameBindingTargetForTest(variable.target, closureLocal));
  }

  ZC_EXPECT(requireClosureFreeVariable(facts, empty).variables.empty());
  const auto& loopFact = requireClosureFreeVariable(facts, fromLoop);
  const auto& matchFact = requireClosureFreeVariable(facts, fromMatch);
  ZC_REQUIRE(loopFact.variables.size() == 1);
  ZC_REQUIRE(matchFact.variables.size() == 1);
  ZC_EXPECT(sameBindingTargetForTest(loopFact.variables[0].target, item));
  ZC_EXPECT(sameBindingTargetForTest(matchFact.variables[0].target, matched));
  const auto itemSites = identifierExpressionsInSubtree(input.tree(), lambdas[1], "item"_zc);
  const auto matchedSites = identifierExpressionsInSubtree(input.tree(), lambdas[2], "matched"_zc);
  ZC_REQUIRE(itemSites.size() == 1);
  ZC_REQUIRE(matchedSites.size() == 1);
  ZC_REQUIRE(loopFact.variables[0].referenceSites.size() == 1);
  ZC_REQUIRE(matchFact.variables[0].referenceSites.size() == 1);
  ZC_EXPECT(loopFact.variables[0].referenceSites[0] == itemSites[0]);
  ZC_EXPECT(matchFact.variables[0].referenceSites[0] == matchedSites[0]);
}

ZC_TEST("ClosureFreeVariables.CapturesModuleOwnedPatternAndLocalReferences") {
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
  auto item =
      requireNamedFrozenBindingTarget(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  auto local = requireNamedFrozenBindingTarget(input, "local"_zc, identity::DefinitionKind::Local);
  const auto itemSites = identifierExpressionsInSubtree(input.tree(), lambdas[0], "item"_zc);
  const auto localSites = identifierExpressionsInSubtree(input.tree(), lambdas[1], "local"_zc);
  ZC_REQUIRE(itemSites.size() == 1);
  ZC_REQUIRE(localSites.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  ZC_REQUIRE(metadata.closureFreeVariables().size() == 2);
  ZC_EXPECT(metadata.explicitClosureCaptures().size() == 0);
  const auto& itemFact = requireOwnerLocalBindingFact(metadata.ownerLocalBindings(), item);
  const auto& localFact = requireOwnerLocalBindingFact(metadata.ownerLocalBindings(), local);
  ZC_EXPECT(itemFact.kind == OwnerLocalBindingKind::PatternBinding);
  ZC_EXPECT(itemFact.activation == DefinitionActivation::LoopPattern);
  ZC_EXPECT(localFact.kind == OwnerLocalBindingKind::Local);
  ZC_EXPECT(localFact.activation == DefinitionActivation::AfterInitializer);
  const auto& itemOwner = metadata.scopes()[itemFact.declaringScope.index()].owner.value();
  const auto& localOwner = metadata.scopes()[localFact.declaringScope.index()].owner.value();
  ZC_REQUIRE(itemOwner.is<ModuleScopeOwner>());
  ZC_REQUIRE(localOwner.is<ModuleScopeOwner>());
  ZC_EXPECT(itemOwner.get<ModuleScopeOwner>().module == input.module());
  ZC_EXPECT(localOwner.get<ModuleScopeOwner>().module == input.module());

  const ast::NodeId capturedSites[] = {itemSites[0], localSites[0]};
  const BindingTarget* capturedTargets[] = {&item, &local};
  for (size_t index = 0; index < zc::size(capturedSites); ++index) {
    const auto& resolution = requireResolution(metadata.nodeBindings(), capturedSites[index]);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    const auto& bound = resolution.value.get<BoundNameResolution>();
    ZC_EXPECT(sameBindingTargetForTest(bound.bindingIdentity, *capturedTargets[index]));
    ZC_EXPECT(sameBindingTargetForTest(bound.canonicalTarget, *capturedTargets[index]));
    const auto& closure = requireAnonymousAt(input, lambdas[index]);
    const auto& row = requireClosureFreeVariable(metadata.closureFreeVariables(), closure);
    ZC_EXPECT(row.closure == closure);
    ZC_REQUIRE(row.variables.size() == 1);
    ZC_EXPECT(sameBindingTargetForTest(row.variables[0].target, *capturedTargets[index]));
    ZC_REQUIRE(row.variables[0].referenceSites.size() == 1);
    ZC_EXPECT(row.variables[0].referenceSites[0] == capturedSites[index]);
  }

  const auto allItemReferences = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(allItemReferences.size() == 2);
  ZC_EXPECT(allItemReferences[1] == itemSites[0]);
  const auto& directResolution = requireResolution(metadata.nodeBindings(), allItemReferences[0]);
  ZC_REQUIRE(directResolution.value.is<BoundNameResolution>());
  const auto& directBound = directResolution.value.get<BoundNameResolution>();
  ZC_EXPECT(sameBindingTargetForTest(directBound.bindingIdentity, item));
  ZC_EXPECT(sameBindingTargetForTest(directBound.canonicalTarget, item));

  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
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
  const auto& outer = requireAnonymousAt(input, lambdas[0]);
  const auto& inner = requireAnonymousAt(input, lambdas[1]);
  auto root =
      requireNamedFrozenBindingTarget(input, "root"_zc, identity::DefinitionKind::Parameter);
  auto outerParameter =
      requireNamedFrozenBindingTarget(input, "outerParam"_zc, identity::DefinitionKind::Parameter);
  auto outerLocal =
      requireNamedFrozenBindingTarget(input, "outerLocal"_zc, identity::DefinitionKind::Local);

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
  const auto& explicitClosure = requireAnonymousAt(input, closures[0]);
  const auto& emptyClosure = requireAnonymousAt(input, closures[1]);
  auto local = requireNamedFrozenBindingTarget(input, "local"_zc, identity::DefinitionKind::Local);
  auto parameter =
      requireNamedFrozenBindingTarget(input, "parameter"_zc, identity::DefinitionKind::Parameter);

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
  ZC_EXPECT(sameBindingTargetForTest(explicitFact.captures[0].target, local));
  ZC_EXPECT(explicitFact.captures[1].item == captureItems[1]);
  ZC_EXPECT(sameBindingTargetForTest(explicitFact.captures[1].target, parameter));
  ZC_EXPECT(input.tree().node(captureItems[0]).payload.words[ast::kCaptureItemModeWord] ==
            static_cast<uint32_t>(ast::CaptureMode::ByValue));
  ZC_EXPECT(input.tree().node(captureItems[1]).payload.words[ast::kCaptureItemModeWord] ==
            static_cast<uint32_t>(ast::CaptureMode::ByRef));
  for (size_t index = 0; index < captureItems.size(); ++index) {
    const auto& resolution = requireResolution(metadata.nodeBindings(), captureItems[index]);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    ZC_EXPECT(sameBindingTargetForTest(resolution.value.get<BoundNameResolution>().bindingIdentity,
                                       explicitFact.captures[index].target));
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
  const auto outerParameter = requireCallableParameterAt(input, parameters[0]);
  const auto innerParameter = requireOwnerLocalBindingAt(input, parameters[1]);
  const auto& closure = requireAnonymousAt(input, closures[0]);
  auto outerParameterTarget = BindingTarget::callableParameter(outerParameter);
  auto innerParameterTarget = BindingTarget::ownerLocal(innerParameter);

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
  ZC_EXPECT(sameBindingTargetForTest(fact.captures[0].target, outerParameterTarget));
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      captureResolution.value.get<BoundNameResolution>().bindingIdentity, outerParameterTarget));
  const auto& bodyResolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(bodyResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      bodyResolution.value.get<BoundNameResolution>().bindingIdentity, innerParameterTarget));

  auto wrongInnerCapture = buildCandidate();
  auto& wrongBound = requireResolution(wrongInnerCapture.nodeBindings.asPtr(), captureItems[0])
                         .value.get<BoundNameResolution>();
  wrongBound.bindingIdentity = BindingTarget::ownerLocal(innerParameter);
  wrongBound.canonicalTarget = BindingTarget::ownerLocal(innerParameter);
  requireExplicitClosureCapture(wrongInnerCapture.explicitClosureCaptures.asPtr(), closure)
      .captures[0]
      .target = BindingTarget::ownerLocal(innerParameter);
  auto wrongInnerResult = BindingDifferentialOracle::verify(input, zc::mv(wrongInnerCapture));
  ZC_EXPECT(requireBinderInvariant(wrongInnerResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ExplicitClosureCaptures.ResolveReceiverAcrossOrdinaryClosureParameter") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Host {\n"
      "  fun run(this: Self) {\n"
      "    const closure = fun(value: i32) use [this] { this; };\n"
      "  }\n"
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
  const auto outerReceiver = requireCallableParameterAt(input, parameters[0]);
  const auto innerReceiver = requireOwnerLocalBindingAt(input, parameters[1]);
  const auto& closure = requireAnonymousAt(input, closures[0]);
  auto outerReceiverTarget = BindingTarget::callableParameter(outerReceiver);

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
  ZC_EXPECT(sameBindingTargetForTest(fact.captures[0].target, outerReceiverTarget));
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      captureResolution.value.get<BoundNameResolution>().bindingIdentity, outerReceiverTarget));
  const auto& bodyBinding = requireThisBinding(metadata.thisBindings(), receivers[0]);
  ZC_EXPECT(bodyBinding.binding.receiverParameter == outerReceiver);

  auto wrongInnerCapture = buildCandidate();
  auto& wrongBound = requireResolution(wrongInnerCapture.nodeBindings.asPtr(), captureItems[0])
                         .value.get<BoundNameResolution>();
  wrongBound.bindingIdentity = BindingTarget::ownerLocal(innerReceiver);
  wrongBound.canonicalTarget = BindingTarget::ownerLocal(innerReceiver);
  requireExplicitClosureCapture(wrongInnerCapture.explicitClosureCaptures.asPtr(), closure)
      .captures[0]
      .target = BindingTarget::ownerLocal(innerReceiver);
  auto wrongInnerResult = BindingDifferentialOracle::verify(input, zc::mv(wrongInnerCapture));
  ZC_EXPECT(requireBinderInvariant(wrongInnerResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingVerifier.RejectsWrongThisExpressionReceiverTarget") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Host {\n"
      "  fun run(this: Self, value: i32) { this; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(parameters.size() == 2);
  ZC_REQUIRE(receivers.size() == 1);
  const auto receiver = requireCallableParameterAt(input, parameters[0]);
  const auto ordinaryParameter = requireCallableParameterAt(input, parameters[1]);
  ZC_EXPECT(receiver != ordinaryParameter);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("nearest receiver verifier fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };

  auto baseline = buildCandidate();
  const auto& receiverBinding = requireThisBinding(baseline.thisBindings.asPtr(), receivers[0]);
  ZC_EXPECT(receiverBinding.binding.receiverParameter == receiver);
  auto verified = BindingVerifier::verify(input, zc::mv(baseline));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());

  auto wrongTarget = buildCandidate();
  auto& wrongReceiver = requireThisBinding(wrongTarget.thisBindings.asPtr(), receivers[0]);
  wrongReceiver.binding.receiverParameter = ordinaryParameter;
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
  const auto& closure = requireAnonymousAt(input, closures[0]);
  auto outerParameter =
      requireNamedFrozenBindingTarget(input, "x"_zc, identity::DefinitionKind::Parameter);
  auto blockLocal = requireNamedFrozenBindingTarget(input, "x"_zc, identity::DefinitionKind::Local);

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
  ZC_EXPECT(sameBindingTargetForTest(fact.captures[0].target, blockLocal));
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      captureResolution.value.get<BoundNameResolution>().bindingIdentity, blockLocal));
  const auto& bodyResolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(bodyResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      bodyResolution.value.get<BoundNameResolution>().bindingIdentity, blockLocal));

  auto wrongOuterCapture = buildCandidate();
  auto& wrongBound = requireResolution(wrongOuterCapture.nodeBindings.asPtr(), captureItems[0])
                         .value.get<BoundNameResolution>();
  wrongBound.bindingIdentity = outerParameter.clone();
  wrongBound.canonicalTarget = outerParameter.clone();
  requireExplicitClosureCapture(wrongOuterCapture.explicitClosureCaptures.asPtr(), closure)
      .captures[0]
      .target = outerParameter.clone();
  auto wrongOuterResult = BindingDifferentialOracle::verify(input, zc::mv(wrongOuterCapture));
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
                                                    requireAnonymousAt(input, closures[index]));
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
      "class Host {\n"
      "  fun outer(this: Self) { const closure = fun() use [] { this; }; }\n"
      "}\n"_zc);
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
                                                  requireAnonymousAt(input, closures[0]));
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
      "class Host {\n"
      "  fun owner(this: Self) {}\n"
      "  fun observer() { this; }\n"
      "}\n"_zc);
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
      "class Host {\n"
      "  fun outer(this: Self) { const closure = fun() { this; }; }\n"
      "}\n"_zc);
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
  const auto receiver = requireCallableParameterAt(input, parameters[0]);
  const auto& closure = requireAnonymousAt(input, closures[0]);
  auto receiverTarget = BindingTarget::callableParameter(receiver);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  ZC_EXPECT(metadata.explicitClosureCaptures().size() == 0);
  ZC_REQUIRE(metadata.closureFreeVariables().size() == 1);
  const auto& row = requireClosureFreeVariable(metadata.closureFreeVariables(), closure);
  ZC_REQUIRE(row.variables.size() == 1);
  const auto& variable = requireFreeVariable(row, receiverTarget);
  ZC_REQUIRE(variable.referenceSites.size() == 1);
  ZC_EXPECT(variable.referenceSites[0] == receivers[0]);
  const auto& binding = requireThisBinding(metadata.thisBindings(), receivers[0]);
  ZC_EXPECT(binding.binding.receiverParameter == receiver);

  const auto& parameterSyntax = input.tree().node(parameters[0]);
  const ast::NodeId type(parameterSyntax.payload.words[ast::kFunctionParameterDeclTyWord]);
  const auto& typeSyntax = input.tree().node(type);
  const ast::NodeId selfPath(typeSyntax.payload.words[ast::kNamedTypeExprPathWord]);
  auto additionalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  additionalCandidate.get<BindingMetadataCandidate>().nodeBindings.add(
      BindingResolution{selfPath, BindingResolutionValue(FailedBindingResolution{0})});
  auto additional =
      BindingVerifier::verify(input, zc::mv(additionalCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireBinderInvariant(additional).kind == BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ExplicitClosureCaptures.BindReceiverAndThisExpression") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Host {\n"
      "  fun make(this: Self) { const closure = fun() use [this] { this; }; }\n"
      "}\n"_zc);
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
  const auto& closure = requireAnonymousAt(input, closures[0]);
  const auto receiver = requireCallableParameterAt(input, parameters[0]);
  auto receiverTarget = BindingTarget::callableParameter(receiver);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  ZC_EXPECT(sameBindingTargetForTest(fact.captures[0].target, receiverTarget));
  const auto& receiverBinding = requireThisBinding(metadata.thisBindings(), receivers[0]);
  ZC_EXPECT(receiverBinding.binding.receiverParameter == receiver);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ReceiverBinding.BareReceiverUsesIntrinsicSelfType") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Host { fun make(this) { this; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto parameters = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionParameterDecl);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(parameters.size() == 1);
  ZC_REQUIRE(receivers.size() == 1);
  ZC_EXPECT(input.parsedModule().functionParameterHasImplicitSelfType(parameters[0]));
  const auto receiver = requireCallableParameterAt(input, parameters[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& binding = requireThisBinding(metadata.thisBindings(), receivers[0]);
  ZC_EXPECT(binding.binding.receiverParameter == receiver);
  ZC_EXPECT(metadata.selfTypes().size() == 0);
  for (const auto& resolution : metadata.nodeBindings()) {
    ZC_EXPECT(resolution.node != receivers[0]);
  }

  const auto& parameterSyntax = input.tree().node(parameters[0]);
  const ast::NodeId type(parameterSyntax.payload.words[ast::kFunctionParameterDeclTyWord]);
  const auto& typeSyntax = input.tree().node(type);
  const ast::NodeId selfPath(typeSyntax.payload.words[ast::kNamedTypeExprPathWord]);
  auto additionalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  additionalCandidate.get<BindingMetadataCandidate>().nodeBindings.add(
      BindingResolution{selfPath, BindingResolutionValue(FailedBindingResolution{0})});
  auto additional =
      BindingVerifier::verify(input, zc::mv(additionalCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireBinderInvariant(additional).kind == BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ContextualSelfBinding.PublishesNominalOwnerAndReceiverFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Box {\n"
      "  fun use(this: Self, value: Self::Item) -> Self { this; }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto classes = nodesOfKind(input.tree(), ast::SyntaxKind::ClassDecl);
  const auto types = nodesOfKind(input.tree(), ast::SyntaxKind::NamedTypeExpr);
  const auto receivers = nodesOfKind(input.tree(), ast::SyntaxKind::ThisExpr);
  ZC_REQUIRE(classes.size() == 1);
  ZC_REQUIRE(types.size() == 3);
  ZC_REQUIRE(receivers.size() == 1);
  const auto owner = requireDefinitionAt(input, classes[0]);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
    ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
    return zc::mv(candidate.get<BindingMetadataCandidate>());
  };
  auto candidate = buildCandidate();
  ZC_REQUIRE(candidate.selfTypes.size() == 3);
  ZC_REQUIRE(candidate.thisBindings.size() == 1);
  for (const auto type : types) {
    const auto& fact = requireSelfType(candidate.selfTypes.asPtr(), type);
    ZC_REQUIRE(fact.owner.is<NominalSelfOwner>());
    ZC_EXPECT(fact.owner.get<NominalSelfOwner>().definition == owner);
  }
  for (const auto& resolution : candidate.nodeBindings) {
    ZC_EXPECT(resolution.node != receivers[0]);
  }
  auto verified = BindingVerifier::verify(input, zc::mv(candidate));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());

  auto missing = buildCandidate();
  missing.selfTypes.removeLast();
  auto missingResult = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ContextualSelfBinding.ReportsModuleScopeSelf") {
  ParsedSource sourceFixture("module root;\nfun invalid(value: Self) {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_EXPECT(value.selfTypes.empty());
  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  const auto failures = rejected.get<SourceRejected>().failures();
  ZC_REQUIRE(failures.size() == 1);
  ZC_EXPECT(failures[0].diagnostic == BinderDiagnosticCode::ContextualSelfOutsideType);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("BindingVerifier.RejectsMalformedContextFailureFacts") {
  ParsedSource selfSource("module root;\nfun invalid(value: Self) {}\n"_zc);
  FrozenFixture selfFixture(selfSource, true);
  auto selfInputResult = verify(selfFixture);
  ZC_REQUIRE(selfInputResult.is<VerifiedBindingInput>());
  auto selfInput = zc::mv(selfInputResult.get<VerifiedBindingInput>());
  auto selfCandidate = BindingBuilder::build(selfInput, *selfSource.diagnostics);
  ZC_REQUIRE(selfCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(selfCandidate.get<BindingMetadataCandidate>().sourceFailures.size() == 1);
  selfCandidate.get<BindingMetadataCandidate>().sourceFailures[0].diagnostic =
      BinderDiagnosticCode::UndefinedIdentifier;
  auto wrongSelfFailure =
      BindingVerifier::verify(selfInput, zc::mv(selfCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireBinderInvariant(wrongSelfFailure).kind ==
            BinderInvariantKind::InvalidBindingFact);

  ParsedSource thisSource("module root;\nfun invalid() { this; }\n"_zc);
  FrozenFixture thisFixture(thisSource, true);
  auto thisInputResult = verify(thisFixture);
  ZC_REQUIRE(thisInputResult.is<VerifiedBindingInput>());
  auto thisInput = zc::mv(thisInputResult.get<VerifiedBindingInput>());
  auto thisCandidate = BindingBuilder::build(thisInput, *thisSource.diagnostics);
  ZC_REQUIRE(thisCandidate.is<BindingMetadataCandidate>());
  ZC_REQUIRE(thisCandidate.get<BindingMetadataCandidate>().sourceFailures.size() == 1);
  auto& failure = thisCandidate.get<BindingMetadataCandidate>().sourceFailures[0];
  failure.emitterOrdinal = (static_cast<uint64_t>(BinderEmitterSite::LabelAndClosure) << 56) |
                           (failure.emitterOrdinal & 0x00ffffffffffffffULL);
  auto wrongThisFailure =
      BindingVerifier::verify(thisInput, zc::mv(thisCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireBinderInvariant(wrongThisFailure).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsMalformedContextualSelfFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Box {\n"
      "  fun use(value: Self::Item) -> Self {}\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto classes = nodesOfKind(input.tree(), ast::SyntaxKind::ClassDecl);
  ZC_REQUIRE(classes.size() == 1);
  const auto owner = requireDefinitionAt(input, classes[0]);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
    ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
    return zc::mv(candidate.get<BindingMetadataCandidate>());
  };

  auto wrongOwner = buildCandidate();
  ZC_REQUIRE(wrongOwner.selfTypes.size() == 2);
  wrongOwner.selfTypes[0].owner = SelfOwner(InterfaceSelfOwner{owner});
  auto wrongOwnerResult = BindingVerifier::verify(input, zc::mv(wrongOwner));
  ZC_EXPECT(requireBinderInvariant(wrongOwnerResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongSource = buildCandidate();
  ZC_REQUIRE(wrongSource.selfTypes.size() == 2);
  wrongSource.selfTypes[0].source = wrongSource.selfTypes[1].source.clone();
  auto wrongSourceResult = BindingVerifier::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(wrongSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ContextualSelfBinding.PublishesInterfaceAndImplOwners") {
  ParsedSource sourceFixture(
      "module root;\n"
      "interface Cloneable { fun clone(this: Self) -> Self; }\n"
      "class Box {}\n"
      "impl Cloneable for Box { fun clone(this: Self) -> Self { this; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true, false, ImplRegistration::Exact);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto interfaces = nodesOfKind(input.tree(), ast::SyntaxKind::InterfaceDecl);
  const auto implementations = nodesOfKind(input.tree(), ast::SyntaxKind::StandaloneImplDecl);
  ZC_REQUIRE(interfaces.size() == 1);
  ZC_REQUIRE(implementations.size() == 1);
  const auto interfaceOwner = requireDefinitionAt(input, interfaces[0]);
  auto implOwner = input.definitions().implAt(implementations[0]);
  ZC_REQUIRE(implOwner != zc::none);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  size_t interfaceFacts = 0;
  size_t implFacts = 0;
  for (const auto& fact : value.selfTypes) {
    if (fact.owner.is<InterfaceSelfOwner>()) {
      ++interfaceFacts;
      ZC_EXPECT(fact.owner.get<InterfaceSelfOwner>().definition == interfaceOwner);
    } else if (fact.owner.is<ImplSelfOwner>()) {
      ++implFacts;
      ZC_EXPECT(fact.owner.get<ImplSelfOwner>().occurrence == ZC_ASSERT_NONNULL(implOwner));
    } else {
      ZC_FAIL_REQUIRE("unexpected contextual Self owner");
    }
  }
  ZC_EXPECT(interfaceFacts == 2);
  ZC_EXPECT(implFacts == 2);
  auto verified = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ReceiverBinding.BindsAttributedReceiverAndPreservesNameToken") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Host {\n"
      "  fun make(#[zom::param::move] this: Self) {\n"
      "    const closure = fun() use [this] { this; };\n"
      "  }\n"
      "}\n"_zc);
  auto snapshot = sourceFixture.snapshot();
  auto expectedReceiverToken = snapshot.span(57, 61);
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
  const auto receiver = requireCallableParameterAt(input, parameters[0]);
  const auto& closure = requireAnonymousAt(input, closures[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  auto receiverTarget = BindingTarget::callableParameter(receiver);
  ZC_EXPECT(sameBindingTargetForTest(fact.captures[0].target, receiverTarget));
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      captureResolution.value.get<BoundNameResolution>().bindingIdentity, receiverTarget));
  const auto& receiverBinding = requireThisBinding(metadata.thisBindings(), receivers[0]);
  ZC_EXPECT(receiverBinding.binding.receiverParameter == receiver);
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
  const auto parameter = requireCallableParameterAt(input, parameters[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& resolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  auto parameterTarget = BindingTarget::callableParameter(parameter);
  ZC_EXPECT(sameBindingTargetForTest(resolution.value.get<BoundNameResolution>().bindingIdentity,
                                     parameterTarget));
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ReceiverBinding.BindsReceiverAfterStackedOuterAttributes") {
  ParsedSource sourceFixture(
      "module root;\n"
      "class Host {\n"
      "  fun make(#[audit::first] #[zom::param::move] this: Self) {\n"
      "    const closure = fun() use [this] { this; };\n"
      "  }\n"
      "}\n"_zc);
  auto snapshot = sourceFixture.snapshot();
  auto expectedReceiverToken = snapshot.span(73, 77);
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
  const auto receiver = requireCallableParameterAt(input, parameters[0]);
  const auto& closure = requireAnonymousAt(input, closures[0]);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
  ZC_REQUIRE(fact.captures.size() == 1);
  auto receiverTarget = BindingTarget::callableParameter(receiver);
  ZC_EXPECT(sameBindingTargetForTest(fact.captures[0].target, receiverTarget));
  const auto& captureResolution = requireResolution(metadata.nodeBindings(), captureItems[0]);
  ZC_REQUIRE(captureResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      captureResolution.value.get<BoundNameResolution>().bindingIdentity, receiverTarget));
  const auto& receiverBinding = requireThisBinding(metadata.thisBindings(), receivers[0]);
  ZC_EXPECT(receiverBinding.binding.receiverParameter == receiver);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("ReceiverBinding.RejectsMissingReceiver") {
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
                                                    requireAnonymousAt(input, closures[0]));
  const auto& partial = requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(),
                                                      requireAnonymousAt(input, closures[1]));
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
                                                    requireAnonymousAt(input, closures[0]));
  const auto& inner = requireExplicitClosureCapture(value.explicitClosureCaptures.asPtr(),
                                                    requireAnonymousAt(input, closures[1]));
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

ZC_TEST("ExplicitClosureCaptures.CapturesModuleOwnedPatternAndLocalItems") {
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
  auto item =
      requireNamedFrozenBindingTarget(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  auto local = requireNamedFrozenBindingTarget(input, "local"_zc, identity::DefinitionKind::Local);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  ZC_REQUIRE(metadata.explicitClosureCaptures().size() == 2);
  ZC_EXPECT(metadata.closureFreeVariables().size() == 0);
  const auto& itemFact = requireOwnerLocalBindingFact(metadata.ownerLocalBindings(), item);
  const auto& localFact = requireOwnerLocalBindingFact(metadata.ownerLocalBindings(), local);
  ZC_EXPECT(itemFact.kind == OwnerLocalBindingKind::PatternBinding);
  ZC_EXPECT(itemFact.activation == DefinitionActivation::LoopPattern);
  ZC_EXPECT(localFact.kind == OwnerLocalBindingKind::Local);
  ZC_EXPECT(localFact.activation == DefinitionActivation::AfterInitializer);
  const auto& itemOwner = metadata.scopes()[itemFact.declaringScope.index()].owner.value();
  const auto& localOwner = metadata.scopes()[localFact.declaringScope.index()].owner.value();
  ZC_REQUIRE(itemOwner.is<ModuleScopeOwner>());
  ZC_REQUIRE(localOwner.is<ModuleScopeOwner>());
  ZC_EXPECT(itemOwner.get<ModuleScopeOwner>().module == input.module());
  ZC_EXPECT(localOwner.get<ModuleScopeOwner>().module == input.module());

  const BindingTarget* capturedTargets[] = {&item, &local};
  for (size_t index = 0; index < captureItems.size(); ++index) {
    const auto& resolution = requireResolution(metadata.nodeBindings(), captureItems[index]);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    const auto& bound = resolution.value.get<BoundNameResolution>();
    ZC_EXPECT(sameBindingTargetForTest(bound.bindingIdentity, *capturedTargets[index]));
    ZC_EXPECT(sameBindingTargetForTest(bound.canonicalTarget, *capturedTargets[index]));
    const auto& closure = requireAnonymousAt(input, closureNodes[index]);
    const auto& row = requireExplicitClosureCapture(metadata.explicitClosureCaptures(), closure);
    ZC_EXPECT(row.closure == closure);
    ZC_REQUIRE(row.captures.size() == 1);
    ZC_EXPECT(row.captures[0].item == captureItems[index]);
    ZC_EXPECT(sameBindingTargetForTest(row.captures[0].target, *capturedTargets[index]));
    const auto& closureSyntax = input.tree().node(closureNodes[index]);
    const ast::NodeId captureList(
        closureSyntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
    ZC_EXPECT(row.captureList == captureList);
    auto rowSource = input.parsedModule().spanFor(input.tree().node(captureList).range);
    ZC_REQUIRE(rowSource != zc::none);
    ZC_IF_SOME(span, rowSource) { ZC_EXPECT(sameSpan(row.source, span)); }
  }

  const auto directItemReferences = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(directItemReferences.size() == 1);
  const auto& directResolution =
      requireResolution(metadata.nodeBindings(), directItemReferences[0]);
  ZC_REQUIRE(directResolution.value.is<BoundNameResolution>());
  const auto& directBound = directResolution.value.get<BoundNameResolution>();
  ZC_EXPECT(sameBindingTargetForTest(directBound.bindingIdentity, item));
  ZC_EXPECT(sameBindingTargetForTest(directBound.canonicalTarget, item));

  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
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

ZC_TEST("BindingDifferentialOracle.RejectsMalformedExplicitCaptureFailures") {
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
  auto wrongFailedSiteResult = BindingDifferentialOracle::verify(input, zc::mv(wrongFailedSite));
  ZC_EXPECT(requireBinderInvariant(wrongFailedSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongFailedSchemaOrdinal = buildCandidate();
  auto& failedSchema =
      wrongFailedSchemaOrdinal.sourceFailures[missingFailureIndex(wrongFailedSchemaOrdinal)];
  failedSchema.emitterOrdinal ^= uint64_t{1} << 16;
  auto wrongFailedSchemaResult =
      BindingDifferentialOracle::verify(input, zc::mv(wrongFailedSchemaOrdinal));
  ZC_EXPECT(requireBinderInvariant(wrongFailedSchemaResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongFailedLocalOrdinal = buildCandidate();
  auto& failedLocal =
      wrongFailedLocalOrdinal.sourceFailures[missingFailureIndex(wrongFailedLocalOrdinal)];
  failedLocal.emitterOrdinal = (failedLocal.emitterOrdinal & 0xffffffffffff0000ULL) | 1;
  auto wrongFailedLocalResult =
      BindingDifferentialOracle::verify(input, zc::mv(wrongFailedLocalOrdinal));
  ZC_EXPECT(requireBinderInvariant(wrongFailedLocalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongFailedPrimary = buildCandidate();
  auto& failedPrimary = wrongFailedPrimary.sourceFailures[missingFailureIndex(wrongFailedPrimary)];
  ZC_IF_SOME(span, firstDuplicateToken) { failedPrimary.primary = span.clone(); }
  auto wrongFailedPrimaryResult =
      BindingDifferentialOracle::verify(input, zc::mv(wrongFailedPrimary));
  ZC_EXPECT(requireBinderInvariant(wrongFailedPrimaryResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongDuplicatePrimary = buildCandidate();
  auto& duplicatePrimary =
      wrongDuplicatePrimary.sourceFailures[duplicateFailureIndex(wrongDuplicatePrimary)];
  ZC_IF_SOME(span, firstDuplicateToken) { duplicatePrimary.primary = span.clone(); }
  auto wrongDuplicatePrimaryResult =
      BindingDifferentialOracle::verify(input, zc::mv(wrongDuplicatePrimary));
  ZC_EXPECT(requireBinderInvariant(wrongDuplicatePrimaryResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongDuplicateNoteSource = buildCandidate();
  auto& duplicateNoteSource =
      wrongDuplicateNoteSource.sourceFailures[duplicateFailureIndex(wrongDuplicateNoteSource)];
  ZC_REQUIRE(duplicateNoteSource.notes.size() == 1);
  ZC_IF_SOME(span, secondDuplicateToken) { duplicateNoteSource.notes[0].source = span.clone(); }
  auto wrongDuplicateNoteSourceResult =
      BindingDifferentialOracle::verify(input, zc::mv(wrongDuplicateNoteSource));
  ZC_EXPECT(requireBinderInvariant(wrongDuplicateNoteSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongDuplicateDiagnostic = buildCandidate();
  auto& duplicateDiagnostic =
      wrongDuplicateDiagnostic.sourceFailures[duplicateFailureIndex(wrongDuplicateDiagnostic)];
  duplicateDiagnostic.diagnostic = BinderDiagnosticCode::UndefinedIdentifier;
  auto wrongDuplicateDiagnosticResult =
      BindingDifferentialOracle::verify(input, zc::mv(wrongDuplicateDiagnostic));
  ZC_EXPECT(requireBinderInvariant(wrongDuplicateDiagnosticResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongDuplicateNoteDiagnostic = buildCandidate();
  auto& duplicateNoteDiagnostic =
      wrongDuplicateNoteDiagnostic
          .sourceFailures[duplicateFailureIndex(wrongDuplicateNoteDiagnostic)];
  ZC_REQUIRE(duplicateNoteDiagnostic.notes.size() == 1);
  duplicateNoteDiagnostic.notes[0].diagnostic = BinderDiagnosticCode::UndefinedIdentifier;
  auto wrongDuplicateNoteDiagnosticResult =
      BindingDifferentialOracle::verify(input, zc::mv(wrongDuplicateNoteDiagnostic));
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
  const auto& explicitClosure = requireAnonymousAt(input, explicitNodes[0]);
  const auto& outerClosure = requireAnonymousAt(input, lambdaNodes[0]);
  const auto& innerClosure = requireAnonymousAt(input, lambdaNodes[1]);
  auto valueDefinition =
      requireNamedFrozenBindingTarget(input, "value"_zc, identity::DefinitionKind::Parameter);

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
  ZC_EXPECT(sameBindingTargetForTest(explicitFact.captures[0].target, valueDefinition));
  const auto& outer = requireClosureFreeVariable(metadata.closureFreeVariables(), outerClosure);
  const auto& inner = requireClosureFreeVariable(metadata.closureFreeVariables(), innerClosure);
  ZC_REQUIRE(requireFreeVariable(outer, valueDefinition).referenceSites.size() == 3);
  ZC_REQUIRE(requireFreeVariable(inner, valueDefinition).referenceSites.size() == 1);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingDifferentialOracle.RejectsMalformedExplicitClosureCaptureFacts") {
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
  const auto& one = requireAnonymousAt(input, closures[0]);
  const auto& two = requireAnonymousAt(input, closures[1]);
  auto second =
      requireNamedFrozenBindingTarget(input, "second"_zc, identity::DefinitionKind::Parameter);

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
  auto missingRowResult = BindingDifferentialOracle::verify(input, zc::mv(missingRow));
  ZC_EXPECT(requireBinderInvariant(missingRowResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalRow = buildCandidate();
  additionalRow.explicitClosureCaptures.add(
      cloneExplicitClosureCapture(additionalRow.explicitClosureCaptures[0]));
  auto additionalRowResult = BindingDifferentialOracle::verify(input, zc::mv(additionalRow));
  ZC_EXPECT(requireBinderInvariant(additionalRowResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedRows = buildCandidate();
  auto displacedRow = zc::mv(reorderedRows.explicitClosureCaptures[0]);
  reorderedRows.explicitClosureCaptures[0] = zc::mv(reorderedRows.explicitClosureCaptures[1]);
  reorderedRows.explicitClosureCaptures[1] = zc::mv(displacedRow);
  auto reorderedRowsResult = BindingDifferentialOracle::verify(input, zc::mv(reorderedRows));
  ZC_EXPECT(requireBinderInvariant(reorderedRowsResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongClosure = buildCandidate();
  wrongClosure.explicitClosureCaptures[0].closure = two.clone();
  auto wrongClosureResult = BindingDifferentialOracle::verify(input, zc::mv(wrongClosure));
  ZC_EXPECT(requireBinderInvariant(wrongClosureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongList = buildCandidate();
  wrongList.explicitClosureCaptures[0].captureList =
      wrongList.explicitClosureCaptures[1].captureList;
  auto wrongListResult = BindingDifferentialOracle::verify(input, zc::mv(wrongList));
  ZC_EXPECT(requireBinderInvariant(wrongListResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongRowSource = buildCandidate();
  wrongRowSource.explicitClosureCaptures[0].source =
      wrongRowSource.explicitClosureCaptures[1].source.clone();
  auto wrongRowSourceResult = BindingDifferentialOracle::verify(input, zc::mv(wrongRowSource));
  ZC_EXPECT(requireBinderInvariant(wrongRowSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingCapture = buildCandidate();
  auto& missingCaptureRow =
      requireExplicitClosureCapture(missingCapture.explicitClosureCaptures.asPtr(), one);
  ZC_REQUIRE(missingCaptureRow.captures.size() == 2);
  missingCaptureRow.captures.removeLast();
  auto missingCaptureResult = BindingDifferentialOracle::verify(input, zc::mv(missingCapture));
  ZC_EXPECT(requireBinderInvariant(missingCaptureResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalCapture = buildCandidate();
  auto& additionalCaptureRow =
      requireExplicitClosureCapture(additionalCapture.explicitClosureCaptures.asPtr(), one);
  additionalCaptureRow.captures.add(cloneExplicitCapture(additionalCaptureRow.captures[0]));
  auto additionalCaptureResult =
      BindingDifferentialOracle::verify(input, zc::mv(additionalCapture));
  ZC_EXPECT(requireBinderInvariant(additionalCaptureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedCaptures = buildCandidate();
  auto& reorderedCaptureRow =
      requireExplicitClosureCapture(reorderedCaptures.explicitClosureCaptures.asPtr(), one);
  auto displacedCapture = zc::mv(reorderedCaptureRow.captures[0]);
  reorderedCaptureRow.captures[0] = zc::mv(reorderedCaptureRow.captures[1]);
  reorderedCaptureRow.captures[1] = zc::mv(displacedCapture);
  auto reorderedCaptureResult = BindingDifferentialOracle::verify(input, zc::mv(reorderedCaptures));
  ZC_EXPECT(requireBinderInvariant(reorderedCaptureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongTarget = buildCandidate();
  requireExplicitClosureCapture(wrongTarget.explicitClosureCaptures.asPtr(), one)
      .captures[0]
      .target = second.clone();
  auto wrongTargetResult = BindingDifferentialOracle::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(wrongTargetResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongItem = buildCandidate();
  requireExplicitClosureCapture(wrongItem.explicitClosureCaptures.asPtr(), one).captures[0].item =
      captureItems[1];
  auto wrongItemResult = BindingDifferentialOracle::verify(input, zc::mv(wrongItem));
  ZC_EXPECT(requireBinderInvariant(wrongItemResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongSource = buildCandidate();
  auto& wrongSourceRow =
      requireExplicitClosureCapture(wrongSource.explicitClosureCaptures.asPtr(), one);
  wrongSourceRow.captures[0].source = wrongSourceRow.source.clone();
  auto wrongSourceResult = BindingDifferentialOracle::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(wrongSourceResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongResolution = buildCandidate();
  auto& bound = requireResolution(wrongResolution.nodeBindings.asPtr(), captureItems[0])
                    .value.get<BoundNameResolution>();
  bound.origin = BindingOrigin::ImportAlias;
  auto wrongResolutionResult = BindingDifferentialOracle::verify(input, zc::mv(wrongResolution));
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
  auto missingResolutionResult =
      BindingDifferentialOracle::verify(input, zc::mv(missingResolution));
  ZC_EXPECT(requireBinderInvariant(missingResolutionResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto overlappingPartition = buildCandidate();
  zc::Vector<FreeVariableFact> noVariables;
  overlappingPartition.closureFreeVariables.add(
      ClosureFreeVariableFact{one.clone(), zc::mv(noVariables)});
  auto overlappingPartitionResult =
      BindingDifferentialOracle::verify(input, zc::mv(overlappingPartition));
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
      "fun other(value: i32) { const closure = fun() use [value] {}; }\n"_zc);
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
  auto foreignClosure = foreign.explicitClosureCaptures[0].closure.clone();
  auto foreignTarget = foreign.explicitClosureCaptures[0].captures[0].target.clone();

  local.explicitClosureCaptures[0].closure = zc::mv(foreignClosure);
  auto closureRejected = BindingVerifier::verify(input, zc::mv(local));
  ZC_EXPECT(requireBinderInvariant(closureRejected).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto targetCandidate = BindingBuilder::build(input, *localSource.diagnostics);
  ZC_REQUIRE(targetCandidate.is<BindingMetadataCandidate>());
  auto& target = targetCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(target.explicitClosureCaptures.size() == 1);
  ZC_REQUIRE(target.explicitClosureCaptures[0].captures.size() == 1);
  target.explicitClosureCaptures[0].captures[0].target = zc::mv(foreignTarget);
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
  auto closureResult = ClosureFreeVariableBuilder::build(
      input, arena, value.definitions.asPtr(), value.callableParameters.asPtr(),
      value.ownerLocalBindings.asPtr(), value.nodeBindings.asPtr(), value.thisBindings.asPtr());
  ZC_REQUIRE(closureResult.is<BinderInvariantFact>());
  const auto& fact = closureResult.get<BinderInvariantFact>();
  ZC_EXPECT(fact.kind == BinderInvariantKind::MalformedScopeGraph);
  ZC_EXPECT(fact.emitterSite == BinderEmitterSite::LabelAndClosure);
  ZC_EXPECT(fact.schemaPreorderOrdinal == schemaPreorderOrdinal(input.tree(), references[0]));
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingDifferentialOracle.RejectsMalformedClosureFreeVariableFacts") {
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
  const auto& outer = requireAnonymousAt(input, lambdas[0]);
  const auto& inner = requireAnonymousAt(input, lambdas[1]);
  auto first =
      requireNamedFrozenBindingTarget(input, "first"_zc, identity::DefinitionKind::Parameter);
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
  auto missingClosureResult = BindingDifferentialOracle::verify(input, zc::mv(missingClosure));
  ZC_EXPECT(requireBinderInvariant(missingClosureResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalClosure = buildCandidate();
  auto duplicateClosure = cloneClosureFreeVariable(additionalClosure.closureFreeVariables[0]);
  additionalClosure.closureFreeVariables.add(zc::mv(duplicateClosure));
  auto additionalClosureResult =
      BindingDifferentialOracle::verify(input, zc::mv(additionalClosure));
  ZC_EXPECT(requireBinderInvariant(additionalClosureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedClosures = buildCandidate();
  auto displacedClosure = zc::mv(reorderedClosures.closureFreeVariables[0]);
  reorderedClosures.closureFreeVariables[0] = zc::mv(reorderedClosures.closureFreeVariables[1]);
  reorderedClosures.closureFreeVariables[1] = zc::mv(displacedClosure);
  auto reorderedClosureResult = BindingDifferentialOracle::verify(input, zc::mv(reorderedClosures));
  ZC_EXPECT(requireBinderInvariant(reorderedClosureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongClosure = buildCandidate();
  requireClosureFreeVariable(wrongClosure.closureFreeVariables.asPtr(), outer).closure =
      inner.clone();
  auto wrongClosureResult = BindingDifferentialOracle::verify(input, zc::mv(wrongClosure));
  ZC_EXPECT(requireBinderInvariant(wrongClosureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingVariable = buildCandidate();
  auto& missingVariableClosure =
      requireClosureFreeVariable(missingVariable.closureFreeVariables.asPtr(), outer);
  ZC_REQUIRE(missingVariableClosure.variables.size() == 2);
  missingVariableClosure.variables.removeLast();
  auto missingVariableResult = BindingDifferentialOracle::verify(input, zc::mv(missingVariable));
  ZC_EXPECT(requireBinderInvariant(missingVariableResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalVariable = buildCandidate();
  auto& additionalVariableClosure =
      requireClosureFreeVariable(additionalVariable.closureFreeVariables.asPtr(), outer);
  auto duplicateVariable = cloneFreeVariable(additionalVariableClosure.variables[0]);
  additionalVariableClosure.variables.add(zc::mv(duplicateVariable));
  auto additionalVariableResult =
      BindingDifferentialOracle::verify(input, zc::mv(additionalVariable));
  ZC_EXPECT(requireBinderInvariant(additionalVariableResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedVariables = buildCandidate();
  auto& reorderedVariableClosure =
      requireClosureFreeVariable(reorderedVariables.closureFreeVariables.asPtr(), outer);
  auto displacedVariable = zc::mv(reorderedVariableClosure.variables[0]);
  reorderedVariableClosure.variables[0] = zc::mv(reorderedVariableClosure.variables[1]);
  reorderedVariableClosure.variables[1] = zc::mv(displacedVariable);
  auto reorderedVariableResult =
      BindingDifferentialOracle::verify(input, zc::mv(reorderedVariables));
  ZC_EXPECT(requireBinderInvariant(reorderedVariableResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongTarget = buildCandidate();
  auto& wrongTargetClosure =
      requireClosureFreeVariable(wrongTarget.closureFreeVariables.asPtr(), outer);
  wrongTargetClosure.variables[0].target = BindingTarget::definition(run);
  auto wrongTargetResult = BindingDifferentialOracle::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(wrongTargetResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto missingSite = buildCandidate();
  auto& missingSiteFact = requireFreeVariable(
      requireClosureFreeVariable(missingSite.closureFreeVariables.asPtr(), outer), first);
  ZC_REQUIRE(missingSiteFact.referenceSites.size() >= 2);
  missingSiteFact.referenceSites.removeLast();
  auto missingSiteResult = BindingDifferentialOracle::verify(input, zc::mv(missingSite));
  ZC_EXPECT(requireBinderInvariant(missingSiteResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalSite = buildCandidate();
  auto& additionalSiteFact = requireFreeVariable(
      requireClosureFreeVariable(additionalSite.closureFreeVariables.asPtr(), outer), first);
  additionalSiteFact.referenceSites.add(additionalSiteFact.referenceSites[0]);
  auto additionalSiteResult = BindingDifferentialOracle::verify(input, zc::mv(additionalSite));
  ZC_EXPECT(requireBinderInvariant(additionalSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedSites = buildCandidate();
  auto& reorderedSiteFact = requireFreeVariable(
      requireClosureFreeVariable(reorderedSites.closureFreeVariables.asPtr(), outer), first);
  const auto displacedSite = reorderedSiteFact.referenceSites[0];
  reorderedSiteFact.referenceSites[0] = reorderedSiteFact.referenceSites[1];
  reorderedSiteFact.referenceSites[1] = displacedSite;
  auto reorderedSiteResult = BindingDifferentialOracle::verify(input, zc::mv(reorderedSites));
  ZC_EXPECT(requireBinderInvariant(reorderedSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongSite = buildCandidate();
  auto& wrongSiteFact = requireFreeVariable(
      requireClosureFreeVariable(wrongSite.closureFreeVariables.asPtr(), outer), first);
  wrongSiteFact.referenceSites[0] = secondSites[0];
  auto wrongSiteResult = BindingDifferentialOracle::verify(input, zc::mv(wrongSite));
  ZC_EXPECT(requireBinderInvariant(wrongSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto outsideSite = buildCandidate();
  auto& outsideSiteFact = requireFreeVariable(
      requireClosureFreeVariable(outsideSite.closureFreeVariables.asPtr(), outer), first);
  outsideSiteFact.referenceSites[0] = ast::NodeId(input.tree().nodeCount() + 1);
  auto outsideSiteResult = BindingDifferentialOracle::verify(input, zc::mv(outsideSite));
  ZC_EXPECT(requireBinderInvariant(outsideSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsSemanticallyMalformedClosureCaptureFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "for (let item in [1]) {\n"
      "  let local = item;\n"
      "  const moduleClosure = () => { item; local; };\n"
      "}\n"
      "fun run(first: i32, second: i32) {\n"
      "  const outer = () => { const inner = () => first; };\n"
      "  const explicit = fun() use [second] { second; };\n"
      "  const own = (own: i32) => own;\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto lambdas = nodesOfKind(input.tree(), ast::SyntaxKind::LambdaExpression);
  const auto explicitClosures = nodesOfKind(input.tree(), ast::SyntaxKind::FunctionExpression);
  ZC_REQUIRE(lambdas.size() == 4);
  ZC_REQUIRE(explicitClosures.size() == 1);
  const auto& moduleClosure = requireAnonymousAt(input, lambdas[0]);
  const auto& outerClosure = requireAnonymousAt(input, lambdas[1]);
  const auto& ownClosure = requireAnonymousAt(input, lambdas[3]);
  const auto& explicitClosure = requireAnonymousAt(input, explicitClosures[0]);
  auto item =
      requireNamedFrozenBindingTarget(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  auto local = requireNamedFrozenBindingTarget(input, "local"_zc, identity::DefinitionKind::Local);
  auto first =
      requireNamedFrozenBindingTarget(input, "first"_zc, identity::DefinitionKind::Parameter);
  auto second =
      requireNamedFrozenBindingTarget(input, "second"_zc, identity::DefinitionKind::Parameter);
  auto own = requireNamedFrozenBindingTarget(input, "own"_zc, identity::DefinitionKind::Parameter);
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  const auto ownReferences = identifierExpressions(input.tree(), "own"_zc);
  ZC_REQUIRE(firstReferences.size() == 1);
  ZC_REQUIRE(ownReferences.size() == 1);

  const auto buildCandidate = [&]() -> BindingMetadataCandidate {
    auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
    if (!result.is<BindingMetadataCandidate>()) {
      ZC_FAIL_REQUIRE("production capture mutation fixture failed to build");
    }
    return zc::mv(result.get<BindingMetadataCandidate>());
  };

  auto missingModulePattern = buildCandidate();
  auto& patternSites =
      requireFreeVariable(requireClosureFreeVariable(
                              missingModulePattern.closureFreeVariables.asPtr(), moduleClosure),
                          item)
          .referenceSites;
  ZC_REQUIRE(patternSites.size() == 1);
  patternSites.removeLast();
  auto missingModulePatternResult = BindingVerifier::verify(input, zc::mv(missingModulePattern));
  ZC_EXPECT(requireBinderInvariant(missingModulePatternResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto missingModuleLocal = buildCandidate();
  auto& localSites =
      requireFreeVariable(requireClosureFreeVariable(
                              missingModuleLocal.closureFreeVariables.asPtr(), moduleClosure),
                          local)
          .referenceSites;
  ZC_REQUIRE(localSites.size() == 1);
  localSites.removeLast();
  auto missingModuleLocalResult = BindingVerifier::verify(input, zc::mv(missingModuleLocal));
  ZC_EXPECT(requireBinderInvariant(missingModuleLocalResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto missingNestedPropagation = buildCandidate();
  auto& outerSites =
      requireFreeVariable(requireClosureFreeVariable(
                              missingNestedPropagation.closureFreeVariables.asPtr(), outerClosure),
                          first)
          .referenceSites;
  ZC_REQUIRE(outerSites.size() == 1);
  outerSites.removeLast();
  auto missingNestedPropagationResult =
      BindingVerifier::verify(input, zc::mv(missingNestedPropagation));
  ZC_EXPECT(requireBinderInvariant(missingNestedPropagationResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalModuleCapture = buildCandidate();
  auto& additionalModuleRow = requireClosureFreeVariable(
      additionalModuleCapture.closureFreeVariables.asPtr(), moduleClosure);
  additionalModuleRow.variables.add(cloneFreeVariable(additionalModuleRow.variables[0]));
  auto additionalModuleCaptureResult =
      BindingVerifier::verify(input, zc::mv(additionalModuleCapture));
  ZC_EXPECT(requireBinderInvariant(additionalModuleCaptureResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongReferenceSite = buildCandidate();
  auto& wrongSite = requireFreeVariable(
      requireClosureFreeVariable(wrongReferenceSite.closureFreeVariables.asPtr(), moduleClosure),
      item);
  ZC_REQUIRE(wrongSite.referenceSites.size() == 1);
  wrongSite.referenceSites[0] = firstReferences[0];
  auto wrongReferenceSiteResult = BindingVerifier::verify(input, zc::mv(wrongReferenceSite));
  ZC_EXPECT(requireBinderInvariant(wrongReferenceSiteResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongClosureOwner = buildCandidate();
  requireClosureFreeVariable(wrongClosureOwner.closureFreeVariables.asPtr(), moduleClosure)
      .closure = outerClosure.clone();
  auto wrongClosureOwnerResult = BindingVerifier::verify(input, zc::mv(wrongClosureOwner));
  ZC_EXPECT(requireBinderInvariant(wrongClosureOwnerResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto wrongExplicitTarget = buildCandidate();
  auto& explicitRow = requireExplicitClosureCapture(
      wrongExplicitTarget.explicitClosureCaptures.asPtr(), explicitClosure);
  ZC_REQUIRE(explicitRow.captures.size() == 1);
  explicitRow.captures[0].target = first.clone();
  auto wrongExplicitTargetResult = BindingVerifier::verify(input, zc::mv(wrongExplicitTarget));
  ZC_EXPECT(requireBinderInvariant(wrongExplicitTargetResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto overlappingClosureDomain = buildCandidate();
  zc::Vector<FreeVariableFact> variables;
  variables.add(FreeVariableFact{second.clone(), zc::Vector<ast::NodeId>()});
  overlappingClosureDomain.closureFreeVariables.add(
      ClosureFreeVariableFact{explicitClosure.clone(), zc::mv(variables)});
  auto overlappingClosureDomainResult =
      BindingVerifier::verify(input, zc::mv(overlappingClosureDomain));
  ZC_EXPECT(requireBinderInvariant(overlappingClosureDomainResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto capturesOwnParameter = buildCandidate();
  auto& ownRow =
      requireClosureFreeVariable(capturesOwnParameter.closureFreeVariables.asPtr(), ownClosure);
  zc::Vector<ast::NodeId> ownSites;
  ownSites.add(ownReferences[0]);
  ownRow.variables.add(FreeVariableFact{own.clone(), zc::mv(ownSites)});
  auto capturesOwnParameterResult = BindingVerifier::verify(input, zc::mv(capturesOwnParameter));
  ZC_EXPECT(requireBinderInvariant(capturesOwnParameterResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BindingVerifier.RejectsForeignClosureFreeVariableIdentities") {
  ParsedSource localSource("module root; fun run(value: i32) { const closure = () => value; }"_zc);
  ParsedSource foreignSource(
      "module root; fun other(value: i32) { const closure = () => value; }"_zc);
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
  const auto& localClosure = requireAnonymousAt(localInput, localClosures[0]);
  auto foreignClosure = requireAnonymousAt(foreignInput, foreignClosures[0]).clone();
  auto foreignTarget = requireNamedFrozenBindingTarget(foreignInput, "value"_zc,
                                                       identity::DefinitionKind::Parameter);

  auto closureCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(closureCandidate.is<BindingMetadataCandidate>());
  auto& foreignClosureFact = closureCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(foreignClosureFact.closureFreeVariables.size() == 1);
  foreignClosureFact.closureFreeVariables[0].closure = zc::mv(foreignClosure);
  auto closureResult = BindingVerifier::verify(localInput, zc::mv(foreignClosureFact));
  ZC_EXPECT(requireBinderInvariant(closureResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto targetCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(targetCandidate.is<BindingMetadataCandidate>());
  auto& foreignTargetFact = targetCandidate.get<BindingMetadataCandidate>();
  auto& closure =
      requireClosureFreeVariable(foreignTargetFact.closureFreeVariables.asPtr(), localClosure);
  ZC_REQUIRE(closure.variables.size() == 1);
  closure.variables[0].target = zc::mv(foreignTarget);
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
  for (const auto& fact : metadata.ownerLocalBindings()) {
    if (fact.kind != OwnerLocalBindingKind::PatternBinding) { continue; }
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

ZC_TEST("BindingVerifier.RejectsMissingPatternScopeProjection") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun scan() { for (let item in [1]) { item; } }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();

  bool removed = false;
  for (auto& scope : value.scopes) {
    if (scope.kind != ScopeKind::Loop) { continue; }
    ZC_REQUIRE(scope.bindings.size() == 1);
    scope.bindings.removeLast();
    removed = true;
  }
  ZC_REQUIRE(removed);

  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_EXPECT(requireBinderInvariant(rejected).kind ==
            BinderInvariantKind::MissingRequiredResolution);
}

ZC_TEST("BindingActivation.RejectsDuplicatePatternBindings") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun scan(pair: i32) {\n"
      "  match (pair) { when (item, item) => { item; } }\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto& value = candidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(value.sourceFailures.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].diagnostic == BinderDiagnosticCode::RedeclareVariable);
  ZC_REQUIRE(value.sourceFailures[0].notes.size() == 1);
  ZC_EXPECT(value.sourceFailures[0].notes[0].diagnostic ==
            BinderDiagnosticCode::PreviousDeclarationHere);
  size_t armBindingCount = 0;
  for (const auto& scope : value.scopes) {
    if (scope.kind == ScopeKind::MatchArm) { armBindingCount += scope.bindings.size(); }
  }
  ZC_EXPECT(armBindingCount == 1);

  auto rejected = BindingVerifier::verify(input, zc::mv(value));
  ZC_REQUIRE(rejected.is<SourceRejected>());
  ZC_EXPECT(rejected.get<SourceRejected>().failures().size() == 1);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
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
  for (const auto& fact : metadata.ownerLocalBindings()) {
    if (fact.kind != OwnerLocalBindingKind::Local) { continue; }
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
  const auto firstTarget = requireScopeBindingTarget(metadata.scopes(), "first"_zc);
  const auto secondTarget = requireScopeBindingTarget(metadata.scopes(), "second"_zc);
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  const auto secondReferences = identifierExpressions(input.tree(), "second"_zc);
  ZC_REQUIRE(firstReferences.size() == 1);
  ZC_REQUIRE(secondReferences.size() == 1);
  const auto& firstResolution = requireResolution(metadata.nodeBindings(), firstReferences[0]);
  const auto& secondResolution = requireResolution(metadata.nodeBindings(), secondReferences[0]);
  ZC_REQUIRE(firstResolution.value.is<BoundNameResolution>());
  ZC_REQUIRE(secondResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      firstResolution.value.get<BoundNameResolution>().bindingIdentity, firstTarget));
  ZC_EXPECT(sameBindingTargetForTest(
      secondResolution.value.get<BoundNameResolution>().bindingIdentity, secondTarget));
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
  auto outerDefinition = requireScopeBindingTarget(metadata.scopes(), "value"_zc, 0);
  auto innerDefinition = requireScopeBindingTarget(metadata.scopes(), "value"_zc, 1);
  ZC_REQUIRE(metadata.shadowTargets().size() == 1);
  ZC_EXPECT(sameBindingTargetForTest(metadata.shadowTargets()[0].binding, innerDefinition));
  ZC_EXPECT(sameBindingTargetForTest(metadata.shadowTargets()[0].target, outerDefinition));
  const auto references = identifierExpressions(input.tree(), "value"_zc);
  ZC_REQUIRE(references.size() == 3);
  const auto& initializerResolution = requireResolution(metadata.nodeBindings(), references[0]);
  const auto& innerResolution = requireResolution(metadata.nodeBindings(), references[1]);
  const auto& outerResolution = requireResolution(metadata.nodeBindings(), references[2]);
  ZC_REQUIRE(initializerResolution.value.is<BoundNameResolution>());
  ZC_REQUIRE(innerResolution.value.is<BoundNameResolution>());
  ZC_REQUIRE(outerResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      initializerResolution.value.get<BoundNameResolution>().bindingIdentity, outerDefinition));
  ZC_EXPECT(sameBindingTargetForTest(
      innerResolution.value.get<BoundNameResolution>().bindingIdentity, innerDefinition));
  ZC_EXPECT(sameBindingTargetForTest(
      outerResolution.value.get<BoundNameResolution>().bindingIdentity, outerDefinition));
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
      requireNamedFrozenBindingTarget(input, "item"_zc, identity::DefinitionKind::PatternBinding);
  const auto references = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(references.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireOwnerLocalBindingFact(metadata.ownerLocalBindings(), item);
  ZC_EXPECT(fact.kind == OwnerLocalBindingKind::PatternBinding);
  ZC_EXPECT(fact.activation == DefinitionActivation::LoopPattern);
  ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Loop);
  const auto& owner = metadata.scopes()[fact.declaringScope.index()].owner.value();
  ZC_REQUIRE(owner.is<ModuleScopeOwner>());
  ZC_EXPECT(owner.get<ModuleScopeOwner>().module == input.module());
  const auto& resolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  const auto& bound = resolution.value.get<BoundNameResolution>();
  ZC_EXPECT(sameBindingTargetForTest(bound.bindingIdentity, item));
  ZC_EXPECT(sameBindingTargetForTest(bound.canonicalTarget, item));
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
  const auto matched = requireNamedFrozenBindingTarget(input, "matched"_zc,
                                                       identity::DefinitionKind::PatternBinding);
  const auto references = identifierExpressions(input.tree(), "matched"_zc);
  ZC_REQUIRE(references.size() == 2);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireOwnerLocalBindingFact(metadata.ownerLocalBindings(), matched);
  ZC_EXPECT(fact.kind == OwnerLocalBindingKind::PatternBinding);
  ZC_EXPECT(fact.activation == DefinitionActivation::MatchPattern);
  ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::MatchArm);
  const auto& owner = metadata.scopes()[fact.declaringScope.index()].owner.value();
  ZC_REQUIRE(owner.is<ModuleScopeOwner>());
  ZC_EXPECT(owner.get<ModuleScopeOwner>().module == input.module());
  for (const auto reference : references) {
    const auto& resolution = requireResolution(metadata.nodeBindings(), reference);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    const auto& bound = resolution.value.get<BoundNameResolution>();
    ZC_EXPECT(sameBindingTargetForTest(bound.bindingIdentity, matched));
    ZC_EXPECT(sameBindingTargetForTest(bound.canonicalTarget, matched));
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
      requireNamedFrozenBindingTarget(input, "local"_zc, identity::DefinitionKind::Local);
  const auto references = identifierExpressions(input.tree(), "local"_zc);
  ZC_REQUIRE(references.size() == 1);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& metadata = verified.get<VerifiedBindingOutput>().metadata;
  const auto& fact = requireOwnerLocalBindingFact(metadata.ownerLocalBindings(), local);
  ZC_EXPECT(fact.kind == OwnerLocalBindingKind::Local);
  ZC_EXPECT(fact.activation == DefinitionActivation::AfterInitializer);
  ZC_EXPECT(metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Block);
  const auto& owner = metadata.scopes()[fact.declaringScope.index()].owner.value();
  ZC_REQUIRE(owner.is<ModuleScopeOwner>());
  ZC_EXPECT(owner.get<ModuleScopeOwner>().module == input.module());
  const auto& resolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  const auto& bound = resolution.value.get<BoundNameResolution>();
  ZC_EXPECT(sameBindingTargetForTest(bound.bindingIdentity, local));
  ZC_EXPECT(sameBindingTargetForTest(bound.canonicalTarget, local));
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

ZC_TEST("BodyBinding.HoistsNamedFunctionsWithinTheirBlock") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun outer() {\n"
      "  nested();\n"
      "  fun nested() {}\n"
      "  nested();\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto nested =
      requireNamedFrozenDefinition(input, "nested"_zc, identity::DefinitionKind::Function);
  const auto references = identifierExpressions(input.tree(), "nested"_zc);
  ZC_REQUIRE(references.size() == 2);

  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verified = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verified.is<VerifiedBindingOutput>());
  const auto& output = verified.get<VerifiedBindingOutput>();
  const auto& fact = requireDefinitionFact(output.metadata.definitions(), nested);
  ZC_EXPECT(fact.activation == DefinitionActivation::ModuleSkeleton);
  ZC_EXPECT(output.metadata.scopes()[fact.declaringScope.index()].kind == ScopeKind::Block);
  for (const auto reference : references) {
    const auto& resolution = requireResolution(output.metadata.nodeBindings(), reference);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    const auto& bound = resolution.value.get<BoundNameResolution>();
    ZC_EXPECT(requireDefinitionTarget(bound.bindingIdentity) == nested);
    ZC_EXPECT(requireDefinitionTarget(bound.canonicalTarget) == nested);
    ZC_EXPECT(bound.nameSpace == Namespace::Value);
    ZC_EXPECT(bound.origin == BindingOrigin::LocalDeclaration);
  }
  for (const auto& entry : output.surface.visibleEntries()) {
    ZC_EXPECT(requireDefinitionTarget(entry.bindingIdentity) != nested);
  }
  ZC_EXPECT(output.metadata.closureFreeVariables().size() == 0);
  ZC_EXPECT(output.metadata.explicitClosureCaptures().size() == 0);
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
  auto outerDefinition = requireScopeBindingTarget(metadata.scopes(), "item"_zc, 0);
  auto loopDefinition = requireScopeBindingTarget(metadata.scopes(), "item"_zc, 1);
  const auto references = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(references.size() == 2);
  const auto& iterableResolution = requireResolution(metadata.nodeBindings(), references[0]);
  const auto& bodyResolution = requireResolution(metadata.nodeBindings(), references[1]);
  ZC_REQUIRE(iterableResolution.value.is<BoundNameResolution>());
  ZC_REQUIRE(bodyResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      iterableResolution.value.get<BoundNameResolution>().bindingIdentity, outerDefinition));
  ZC_EXPECT(sameBindingTargetForTest(
      bodyResolution.value.get<BoundNameResolution>().bindingIdentity, loopDefinition));
  ZC_REQUIRE(metadata.shadowTargets().size() == 1);
  ZC_EXPECT(sameBindingTargetForTest(metadata.shadowTargets()[0].binding, loopDefinition));
  ZC_EXPECT(sameBindingTargetForTest(metadata.shadowTargets()[0].target, outerDefinition));
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
  auto outerDefinition = requireScopeBindingTarget(metadata.scopes(), "item"_zc, 0);
  auto armDefinition = requireScopeBindingTarget(metadata.scopes(), "item"_zc, 1);
  const auto references = identifierExpressions(input.tree(), "item"_zc);
  ZC_REQUIRE(references.size() == 3);
  const auto& scrutineeResolution = requireResolution(metadata.nodeBindings(), references[0]);
  ZC_REQUIRE(scrutineeResolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(
      scrutineeResolution.value.get<BoundNameResolution>().bindingIdentity, outerDefinition));
  for (size_t index = 1; index < references.size(); ++index) {
    const auto& resolution = requireResolution(metadata.nodeBindings(), references[index]);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    ZC_EXPECT(sameBindingTargetForTest(resolution.value.get<BoundNameResolution>().bindingIdentity,
                                       armDefinition));
  }
  ZC_REQUIRE(metadata.shadowTargets().size() == 1);
  ZC_EXPECT(sameBindingTargetForTest(metadata.shadowTargets()[0].binding, armDefinition));
  ZC_EXPECT(sameBindingTargetForTest(metadata.shadowTargets()[0].target, outerDefinition));
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
  const auto firstTarget = requireScopeBindingTarget(metadata.scopes(), "first"_zc);
  const auto secondTarget = requireScopeBindingTarget(metadata.scopes(), "second"_zc);
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  const auto secondReferences = identifierExpressions(input.tree(), "second"_zc);
  ZC_REQUIRE(firstReferences.size() == 1);
  ZC_REQUIRE(secondReferences.size() == 1);
  ZC_EXPECT(sameBindingTargetForTest(requireResolution(metadata.nodeBindings(), firstReferences[0])
                                         .value.get<BoundNameResolution>()
                                         .bindingIdentity,
                                     firstTarget));
  ZC_EXPECT(sameBindingTargetForTest(requireResolution(metadata.nodeBindings(), secondReferences[0])
                                         .value.get<BoundNameResolution>()
                                         .bindingIdentity,
                                     secondTarget));
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
  const auto target = requireScopeBindingTarget(value.scopes.asPtr(), "value"_zc);
  const auto shorthand = shorthandProperties(input.tree(), "value"_zc);
  const auto explicitValues = identifierExpressions(input.tree(), "value"_zc);
  ZC_REQUIRE(shorthand.size() == 1);
  ZC_REQUIRE(explicitValues.size() == 1);
  for (const auto node : {shorthand[0], explicitValues[0]}) {
    const auto& resolution = requireResolution(value.nodeBindings.asPtr(), node);
    ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
    ZC_EXPECT(resolution.value.get<BoundNameResolution>().nameSpace == Namespace::Value);
    ZC_EXPECT(sameBindingTargetForTest(resolution.value.get<BoundNameResolution>().bindingIdentity,
                                       target));
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

ZC_TEST("BindingDifferentialOracle.RejectsMalformedDeferredMemberFacts") {
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
  auto baseRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongBase));
  ZC_EXPECT(requireBinderInvariant(baseRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto namespaceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(namespaceCandidate.is<BindingMetadataCandidate>());
  auto& wrongNamespace = namespaceCandidate.get<BindingMetadataCandidate>();
  auto& namespaceFact = requireDeferredMember(wrongNamespace.deferredMembers.asPtr(), methods[0]);
  auto& namespaceResolution = requireResolution(wrongNamespace.nodeBindings.asPtr(), methods[0]);
  ZC_REQUIRE(namespaceResolution.value.is<DeferredMemberFact>());
  namespaceFact.expectedNamespaces[0] = Namespace::Type;
  namespaceResolution.value.get<DeferredMemberFact>().expectedNamespaces[0] = Namespace::Type;
  auto namespaceRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongNamespace));
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
  auto genericRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongGeneric));
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
  auto sourceRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(sourceRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto orderCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(orderCandidate.is<BindingMetadataCandidate>());
  auto& wrongOrder = orderCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(wrongOrder.deferredMembers.size() == 2);
  auto firstFact = zc::mv(wrongOrder.deferredMembers[0]);
  wrongOrder.deferredMembers[0] = zc::mv(wrongOrder.deferredMembers[1]);
  wrongOrder.deferredMembers[1] = zc::mv(firstFact);
  auto orderRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongOrder));
  ZC_EXPECT(requireBinderInvariant(orderRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  missing.deferredMembers.removeLast();
  auto missingRejected = BindingDifferentialOracle::verify(input, zc::mv(missing));
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
  auto resolutionRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongResolution));
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

ZC_TEST("StableIdentityPreAdmission.RejectsNonLiteralLaterParameterArrayLength") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(first: i32, second: [i32; first]) {}\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<StableIdentityCandidateSourceFailure>());
  const auto& failure = verification.get<StableIdentityCandidateSourceFailure>();
  ZC_EXPECT(failure.kind == StableIdentityCandidateSourceFailureKind::ConstantExpressionNotAllowed);
  ZC_EXPECT(failure.source.byteStart() == 47);
  ZC_EXPECT(failure.source.byteEnd() == 52);
  emitStableSourceFailure(sourceFixture, failure);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
}

ZC_TEST("StableIdentityPreAdmission.RejectsNonLiteralReturnArrayLength") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run(size: i32) -> [i32; size] {}\n"_zc);
  auto verification = verifyStableCandidates(sourceFixture);
  ZC_REQUIRE(verification.is<StableIdentityCandidateSourceFailure>());
  const auto& failure = verification.get<StableIdentityCandidateSourceFailure>();
  ZC_EXPECT(failure.kind == StableIdentityCandidateSourceFailureKind::ConstantExpressionNotAllowed);
  ZC_EXPECT(failure.source.byteStart() == 41);
  ZC_EXPECT(failure.source.byteEnd() == 45);
  emitStableSourceFailure(sourceFixture, failure);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 1);
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
  const auto valueTarget = requireScopeBindingTarget(metadata.scopes(), "value"_zc);
  const auto valueUses = identifierExpressions(input.tree(), "value"_zc);
  ZC_REQUIRE(valueUses.size() == 1);
  const auto& resolution = requireResolution(metadata.nodeBindings(), valueUses[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  ZC_EXPECT(sameBindingTargetForTest(resolution.value.get<BoundNameResolution>().bindingIdentity,
                                     valueTarget));
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
  ZC_REQUIRE(value.definitions.size() == input.definitions().definitions().size());
  ZC_REQUIRE(value.genericParameters.size() == input.definitions().genericParameters().size());
  ZC_REQUIRE(value.callableParameters.size() == input.definitions().callableParameters().size());
  ZC_REQUIRE(value.ownerLocalBindings.size() == input.definitions().ownerLocalBindings().size());
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

ZC_TEST("BindingVerifier.RejectsMalformedSubordinateBindingFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun alpha<First, Second>(left: First, right: Second);\n"
      "fun beta<Third>(value: Third);\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());

  auto missingGenericCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingGenericCandidate.is<BindingMetadataCandidate>());
  auto& missingGeneric = missingGenericCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missingGeneric.genericParameters.size() >= 3);
  missingGeneric.genericParameters.removeLast();
  auto missingGenericResult = BindingVerifier::verify(input, zc::mv(missingGeneric));
  ZC_EXPECT(requireBinderInvariant(missingGenericResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto missingCallableCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCallableCandidate.is<BindingMetadataCandidate>());
  auto& missingCallable = missingCallableCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missingCallable.callableParameters.size() >= 3);
  missingCallable.callableParameters.removeLast();
  auto missingCallableResult = BindingVerifier::verify(input, zc::mv(missingCallable));
  ZC_EXPECT(requireBinderInvariant(missingCallableResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto reorderedGenericCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(reorderedGenericCandidate.is<BindingMetadataCandidate>());
  auto& reorderedGeneric = reorderedGenericCandidate.get<BindingMetadataCandidate>();
  auto displacedGeneric = zc::mv(reorderedGeneric.genericParameters[0]);
  reorderedGeneric.genericParameters[0] = zc::mv(reorderedGeneric.genericParameters[1]);
  reorderedGeneric.genericParameters[1] = zc::mv(displacedGeneric);
  auto reorderedGenericResult = BindingVerifier::verify(input, zc::mv(reorderedGeneric));
  ZC_EXPECT(requireBinderInvariant(reorderedGenericResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto reorderedCallableCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(reorderedCallableCandidate.is<BindingMetadataCandidate>());
  auto& reorderedCallable = reorderedCallableCandidate.get<BindingMetadataCandidate>();
  auto displacedCallable = zc::mv(reorderedCallable.callableParameters[0]);
  reorderedCallable.callableParameters[0] = zc::mv(reorderedCallable.callableParameters[1]);
  reorderedCallable.callableParameters[1] = zc::mv(displacedCallable);
  auto reorderedCallableResult = BindingVerifier::verify(input, zc::mv(reorderedCallable));
  ZC_EXPECT(requireBinderInvariant(reorderedCallableResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto replacedGenericCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(replacedGenericCandidate.is<BindingMetadataCandidate>());
  auto& replacedGeneric = replacedGenericCandidate.get<BindingMetadataCandidate>();
  replacedGeneric.genericParameters[0].identity = replacedGeneric.genericParameters[1].identity;
  auto replacedGenericResult = BindingVerifier::verify(input, zc::mv(replacedGeneric));
  ZC_EXPECT(requireBinderInvariant(replacedGenericResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto replacedCallableCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(replacedCallableCandidate.is<BindingMetadataCandidate>());
  auto& replacedCallable = replacedCallableCandidate.get<BindingMetadataCandidate>();
  replacedCallable.callableParameters[0].identity = replacedCallable.callableParameters[1].identity;
  auto replacedCallableResult = BindingVerifier::verify(input, zc::mv(replacedCallable));
  ZC_EXPECT(requireBinderInvariant(replacedCallableResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto sourceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(sourceCandidate.is<BindingMetadataCandidate>());
  auto& wrongSource = sourceCandidate.get<BindingMetadataCandidate>();
  wrongSource.genericParameters[0].source = wrongSource.genericParameters[1].source.clone();
  auto sourceResult = BindingVerifier::verify(input, zc::mv(wrongSource));
  ZC_EXPECT(requireBinderInvariant(sourceResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto additionalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  auto& additional = additionalCandidate.get<BindingMetadataCandidate>();
  additional.callableParameters.add(cloneCallableParameterFact(additional.callableParameters[0]));
  auto additionalResult = BindingVerifier::verify(input, zc::mv(additional));
  ZC_EXPECT(requireBinderInvariant(additionalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto additionalGenericCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalGenericCandidate.is<BindingMetadataCandidate>());
  auto& additionalGeneric = additionalGenericCandidate.get<BindingMetadataCandidate>();
  additionalGeneric.genericParameters.add(
      cloneGenericParameterFact(additionalGeneric.genericParameters[0]));
  auto additionalGenericResult = BindingVerifier::verify(input, zc::mv(additionalGeneric));
  ZC_EXPECT(requireBinderInvariant(additionalGenericResult).kind ==
            BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsMalformedOwnerLocalBindingFacts") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun alpha() { let first = 1; let second = 2; }\n"
      "fun beta() { let third = 3; let fourth = 4; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  const auto inventory = input.definitions().ownerLocalBindings();
  ZC_REQUIRE(inventory.size() >= 4);

  size_t sameOwnerLeft = inventory.size();
  size_t sameOwnerRight = inventory.size();
  size_t differentOwner = inventory.size();
  for (size_t left = 0; left < inventory.size(); ++left) {
    for (size_t right = left + 1; right < inventory.size(); ++right) {
      if (inventory[left].key.owner() == inventory[right].key.owner()) {
        if (sameOwnerLeft == inventory.size()) {
          sameOwnerLeft = left;
          sameOwnerRight = right;
        }
      } else if (differentOwner == inventory.size()) {
        differentOwner = right;
      }
    }
  }
  ZC_REQUIRE(sameOwnerLeft < inventory.size());
  ZC_REQUIRE(sameOwnerRight < inventory.size());
  ZC_REQUIRE(differentOwner < inventory.size());

  auto factIndex = [&](zc::ArrayPtr<const OwnerLocalBindingFact> facts,
                       OwnerLocalBindingId binding) {
    for (size_t index = 0; index < facts.size(); ++index) {
      if (facts[index].identity == binding) { return index; }
    }
    return facts.size();
  };

  auto missingCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  auto& missing = missingCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(missing.ownerLocalBindings.size() == inventory.size());
  missing.ownerLocalBindings.removeLast();
  auto missingResult = BindingVerifier::verify(input, zc::mv(missing));
  ZC_EXPECT(requireBinderInvariant(missingResult).kind ==
            BinderInvariantKind::MissingRequiredResolution);

  auto additionalCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  auto& additional = additionalCandidate.get<BindingMetadataCandidate>();
  additional.ownerLocalBindings.add(cloneOwnerLocalBindingFact(additional.ownerLocalBindings[0]));
  auto additionalResult = BindingVerifier::verify(input, zc::mv(additional));
  ZC_EXPECT(requireBinderInvariant(additionalResult).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto keyCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(keyCandidate.is<BindingMetadataCandidate>());
  auto& wrongKey = keyCandidate.get<BindingMetadataCandidate>();
  const auto keyFact =
      factIndex(wrongKey.ownerLocalBindings.asPtr(), inventory[sameOwnerLeft].binding);
  ZC_REQUIRE(keyFact < wrongKey.ownerLocalBindings.size());
  wrongKey.ownerLocalBindings[keyFact].identity = inventory[sameOwnerRight].binding;
  auto keyResult = BindingVerifier::verify(input, zc::mv(wrongKey));
  ZC_EXPECT(requireBinderInvariant(keyResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto siteCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(siteCandidate.is<BindingMetadataCandidate>());
  auto& wrongSite = siteCandidate.get<BindingMetadataCandidate>();
  const auto siteFact =
      factIndex(wrongSite.ownerLocalBindings.asPtr(), inventory[sameOwnerLeft].binding);
  ZC_REQUIRE(siteFact < wrongSite.ownerLocalBindings.size());
  wrongSite.ownerLocalBindings[siteFact].site = inventory[sameOwnerRight].site.clone();
  auto siteResult = BindingVerifier::verify(input, zc::mv(wrongSite));
  ZC_EXPECT(requireBinderInvariant(siteResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto scopeCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(scopeCandidate.is<BindingMetadataCandidate>());
  auto& wrongScope = scopeCandidate.get<BindingMetadataCandidate>();
  const auto scopeFact =
      factIndex(wrongScope.ownerLocalBindings.asPtr(), inventory[sameOwnerLeft].binding);
  const auto foreignScopeFact =
      factIndex(wrongScope.ownerLocalBindings.asPtr(), inventory[differentOwner].binding);
  ZC_REQUIRE(scopeFact < wrongScope.ownerLocalBindings.size());
  ZC_REQUIRE(foreignScopeFact < wrongScope.ownerLocalBindings.size());
  wrongScope.ownerLocalBindings[scopeFact].declaringScope =
      wrongScope.ownerLocalBindings[foreignScopeFact].declaringScope;
  auto scopeResult = BindingVerifier::verify(input, zc::mv(wrongScope));
  ZC_EXPECT(requireBinderInvariant(scopeResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto ownerCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(ownerCandidate.is<BindingMetadataCandidate>());
  auto& wrongOwner = ownerCandidate.get<BindingMetadataCandidate>();
  const auto ownerFact =
      factIndex(wrongOwner.ownerLocalBindings.asPtr(), inventory[sameOwnerLeft].binding);
  ZC_REQUIRE(ownerFact < wrongOwner.ownerLocalBindings.size());
  wrongOwner.ownerLocalBindings[ownerFact].identity = inventory[differentOwner].binding;
  auto ownerResult = BindingVerifier::verify(input, zc::mv(wrongOwner));
  ZC_EXPECT(requireBinderInvariant(ownerResult).kind == BinderInvariantKind::InvalidBindingFact);

  auto enumCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(enumCandidate.is<BindingMetadataCandidate>());
  auto& wrongEnum = enumCandidate.get<BindingMetadataCandidate>();
  wrongEnum.ownerLocalBindings[0].nameSpace = Namespace::Type;
  auto enumResult = BindingVerifier::verify(input, zc::mv(wrongEnum));
  ZC_EXPECT(requireBinderInvariant(enumResult).kind == BinderInvariantKind::InvalidBindingFact);
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
  const auto secondTarget = requireScopeBindingTarget(value.scopes.asPtr(), "second"_zc);
  const auto firstReferences = identifierExpressions(input.tree(), "first"_zc);
  ZC_REQUIRE(firstReferences.size() == 1);
  auto& resolution = requireResolution(value.nodeBindings.asPtr(), firstReferences[0]);
  ZC_REQUIRE(resolution.value.is<BoundNameResolution>());
  resolution.value.get<BoundNameResolution>().bindingIdentity = secondTarget.clone();
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
  const auto secondTarget = requireScopeBindingTarget(wrongCanonical.scopes.asPtr(), "second"_zc);
  auto& canonicalResolution =
      requireResolution(wrongCanonical.nodeBindings.asPtr(), firstReferences[0]);
  ZC_REQUIRE(canonicalResolution.value.is<BoundNameResolution>());
  canonicalResolution.value.get<BoundNameResolution>().canonicalTarget = secondTarget.clone();
  auto canonicalRejected = BindingVerifier::verify(input, zc::mv(wrongCanonical));
  ZC_EXPECT(requireBinderInvariant(canonicalRejected).kind ==
            BinderInvariantKind::InvalidBindingFact);

  auto namespaceCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(namespaceCandidate.is<BindingMetadataCandidate>());
  auto& wrongNamespace = namespaceCandidate.get<BindingMetadataCandidate>();
  auto& namespaceResolution =
      requireResolution(wrongNamespace.nodeBindings.asPtr(), firstReferences[0]);
  namespaceResolution.value.get<BoundNameResolution>().nameSpace = Namespace::Type;
  auto namespaceRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongNamespace));
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

ZC_TEST("BindingDifferentialOracle.RejectsMalformedShadowFacts") {
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
  auto secondDefinition = requireScopeBindingTarget(wrongTarget.scopes.asPtr(), "second"_zc);
  wrongTarget.shadowTargets[0].target = secondDefinition.clone();
  auto targetRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongTarget));
  ZC_EXPECT(requireBinderInvariant(targetRejected).kind == BinderInvariantKind::InvalidBindingFact);

  auto definitionCandidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(definitionCandidate.is<BindingMetadataCandidate>());
  auto& wrongDefinition = definitionCandidate.get<BindingMetadataCandidate>();
  wrongDefinition.shadowTargets[0].binding = secondDefinition.clone();
  auto definitionRejected = BindingDifferentialOracle::verify(input, zc::mv(wrongDefinition));
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

ZC_TEST("BindingDifferentialOracle.RejectsMissingShadowTarget") {
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
  auto rejected = BindingDifferentialOracle::verify(input, zc::mv(value));
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

ZC_TEST("ModuleGraphSourceFailure.RejectsReservedToolchainRootWithVerifiedAnchor") {
  ParsedSource sourceFixture("module core;\n"_zc);
  FrozenFixture fixture(sourceFixture);
  ZC_IF_SOME(parsed, fixture.parsed) {
    ModuleGraphModule moduleInput(module(), fixture.moduleId);
    ParsedModuleGraphInput parsedInput{fixture.moduleId, parsed};
    auto built =
        ModuleGraphSourceFailureBuilder::buildToolchainModuleRootReserved(moduleInput, parsedInput);
    ZC_REQUIRE(built != zc::none);
    ZC_IF_SOME(failure, built) {
      ZC_EXPECT(failure.module().encode().asPtr() == module().encode().asPtr());
      ZC_EXPECT(failure.source().sameAs(parsed.source()));
      ZC_REQUIRE(failure.declaredNamePath().components().size() == 1);
      ZC_EXPECT(failure.declaredNamePath().components()[0] == 0);
      ZC_EXPECT(failure.schemaPreorderOrdinal() == 1);
      ZC_REQUIRE(failure.argument().path().size() == 1);
      ZC_EXPECT(failure.argument().path()[0].text() == "core"_zc);

      auto consumer = zc::heap<ModuleGraphDiagnosticCapture>();
      const auto& capture = *consumer;
      sourceFixture.diagnostics->addConsumer(zc::mv(consumer));
      ZC_EXPECT(canEmitModuleGraphSourceFailure(parsed, failure));
      ZC_EXPECT(emitModuleGraphSourceFailure(*sourceFixture.diagnostics, parsed, failure));
      ZC_EXPECT(capture.count == 1);
      ZC_EXPECT(capture.id == diagnostics::DiagID::ToolchainModuleRootReserved);
      ZC_EXPECT(capture.argument == "core"_zc);
      ZC_EXPECT(capture.rendered == "Module root 'core' is reserved by the compiler toolchain"_zc);
    }
  } else {
    ZC_EXPECT(false);
  }
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
