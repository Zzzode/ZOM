// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-input.h"

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/module-dependency-requests.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::binder {
namespace {

constexpr char kGraphDomain[] = "zom.module-dependency-graph";

void appendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    const uint32_t shift = 56 - index * 8;
    bytes.add(static_cast<uint8_t>((value >> shift) & 0xffu));
  }
}

ModuleGraphInvariantFact failure(ModuleGraphInvariantKind kind,
                                 zc::Maybe<identity::ModuleId>&& requester = zc::none) {
  return ModuleGraphInvariantFact{kind, zc::mv(requester), zc::Vector<uint32_t>(), 1};
}

bool allRegistriesFrozen(const identity::SemanticIdentityRegistrySet& registries) {
  return registries.packages().isFrozen() && registries.crates().isFrozen() &&
         registries.sourceFiles().isFrozen() && registries.modules().isFrozen() &&
         registries.definitions().isFrozen() && registries.impls().isFrozen();
}

const ModuleDependencyRequest& resolutionRequest(const ModulePathResolution& resolution) {
  if (resolution.is<ResolvedModulePath>()) { return resolution.get<ResolvedModulePath>().request; }
  if (resolution.is<MissingModulePath>()) { return resolution.get<MissingModulePath>().request; }
  return resolution.get<AmbiguousModulePath>().request;
}

const VerifiedStructuralResolutionReceipt& resolutionReceipt(
    const ModulePathResolution& resolution) {
  if (resolution.is<ResolvedModulePath>()) { return resolution.get<ResolvedModulePath>().receipt; }
  if (resolution.is<MissingModulePath>()) { return resolution.get<MissingModulePath>().receipt; }
  return resolution.get<AmbiguousModulePath>().receipt;
}

zc::Maybe<zc::Vector<uint32_t>> schemaPreorderOrdinals(const ast::Tree& tree) {
  zc::Vector<uint32_t> ordinals;
  ordinals.resize(tree.nodeCount() + 1);
  for (auto& value : ordinals) { value = UINT32_MAX; }
  uint32_t ordinal = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (node.value >= ordinals.size() || ordinals[node.value] != UINT32_MAX) { return; }
    ordinals[node.value] = ordinal++;
  });
  if (ordinal != tree.nodeCount()) { return zc::none; }
  return zc::mv(ordinals);
}

zc::Maybe<zc::Vector<identity::ModulePathSegment>> normalizedModulePath(const ast::Tree& tree,
                                                                        ast::NodeId path) {
  if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
    return zc::none;
  }
  const auto& syntax = tree.node(path);
  const ast::IdentList segments{syntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                syntax.payload.words[ast::kModulePathSegmentsSizeWord]};
  if (segments.size == 0) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> result(segments.size);
  for (const auto segment : tree.identList(segments)) {
    auto value = identity::ModulePathSegment::fromSource(tree.ident(segment));
    if (value == zc::none) { return zc::none; }
    ZC_IF_SOME(canonical, value) { result.add(zc::mv(canonical)); }
  }
  return zc::mv(result);
}

zc::Array<uint8_t> encodeDependencyEdgeKey(const identity::ModuleKey& requester,
                                           const ModuleDependencyRequest& request,
                                           const identity::ModuleKey& target) {
  identity::CanonicalEncoder encoder;
  requester.encode(encoder);
  encoder.encodeByteString(request.key().encode().asPtr());
  target.encode(encoder);
  return encoder.finish();
}

zc::Vector<identity::ModuleKey> cloneModuleKeys(zc::ArrayPtr<const identity::ModuleKey> keys) {
  zc::Vector<identity::ModuleKey> result(keys.size());
  for (const auto& key : keys) { result.add(key.clone()); }
  return result;
}

ModuleGraphDiagnostic missingDiagnostic(identity::ModuleDependencyKind kind) {
  return kind == identity::ModuleDependencyKind::ForeignReexport
             ? ModuleGraphDiagnostic::ReexportModuleNotFound
             : ModuleGraphDiagnostic::ImportModuleNotFound;
}

ModuleGraphDiagnostic ambiguousDiagnostic(identity::ModuleDependencyKind kind) {
  return kind == identity::ModuleDependencyKind::ForeignReexport
             ? ModuleGraphDiagnostic::ReexportModuleAmbiguous
             : ModuleGraphDiagnostic::ImportModuleAmbiguous;
}

bool sameBindingTarget(const BindingTarget& left, const BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DefinitionBindingTarget>()) {
    return rightValue.is<DefinitionBindingTarget>() &&
           leftValue.get<DefinitionBindingTarget>().definition ==
               rightValue.get<DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<SemanticImportBindingTarget>()) {
    return rightValue.is<SemanticImportBindingTarget>() &&
           leftValue.get<SemanticImportBindingTarget>().binding ==
               rightValue.get<SemanticImportBindingTarget>().binding;
  }
  return rightValue.is<ModuleBindingTarget>() && leftValue.get<ModuleBindingTarget>().module ==
                                                     rightValue.get<ModuleBindingTarget>().module;
}

void encodeBindingName(identity::CanonicalEncoder& encoder, const BindingNameKey& name) {
  encoder.encodeUint8(static_cast<uint8_t>(name.nameSpace()));
  name.name().encode(encoder);
}

bool encodeSurfaceTarget(identity::CanonicalEncoder& encoder,
                         const identity::SemanticIdentityRegistrySet& registries,
                         identity::SemanticContextBrand context, const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    const auto definition = value.get<DefinitionBindingTarget>().definition;
    if (!definition.belongsTo(context)) { return false; }
    auto key = registries.definitions().lookup(definition);
    if (key == zc::none) { return false; }
    encoder.encodeUint8(0x01);
    ZC_IF_SOME(keyValue, key) {
      keyValue.encode(encoder);
      return true;
    }
    ZC_UNREACHABLE;
  }
  if (value.is<SemanticImportBindingTarget>()) {
    encoder.encodeUint8(0x02);
    encoder.encodeByteString(value.get<SemanticImportBindingTarget>().binding.encode().asPtr());
    return true;
  }
  const auto module = value.get<ModuleBindingTarget>().module;
  if (!module.belongsTo(context)) { return false; }
  auto key = registries.modules().lookup(module);
  if (key == zc::none) { return false; }
  encoder.encodeUint8(0x03);
  ZC_IF_SOME(keyValue, key) {
    keyValue.encode(encoder);
    return true;
  }
  ZC_UNREACHABLE;
}

bool encodeSurfaceVisibility(identity::CanonicalEncoder& encoder,
                             const identity::SemanticIdentityRegistrySet& registries,
                             identity::SemanticContextBrand context,
                             const VisibilityEnvelope& visibility) {
  const auto& value = visibility.value();
  if (value.is<ModuleVisibility>()) {
    const auto module = value.get<ModuleVisibility>().module;
    if (!module.belongsTo(context)) { return false; }
    auto key = registries.modules().lookup(module);
    if (key == zc::none) { return false; }
    encoder.encodeUint8(0x01);
    ZC_IF_SOME(keyValue, key) {
      keyValue.encode(encoder);
      return true;
    }
    ZC_UNREACHABLE;
  }
  encoder.encodeUint8(0x02);
  return true;
}

void encodeOptionalSpan(identity::CanonicalEncoder& encoder,
                        const zc::Maybe<identity::SourceSpan>& span) {
  ZC_IF_SOME(value, span) {
    encoder.encodeSome();
    value.encode(encoder);
    return;
  }
  encoder.encodeNone();
}

bool moduleContainsSpan(const VerifiedModuleGraphView& graph, identity::ModuleId module,
                        const identity::SourceSpan& span) {
  auto source = graph.sourceFile(module);
  ZC_IF_SOME(value, source) { return span.belongsTo(value); }
  return false;
}

bool targetContainsSpan(const identity::SemanticIdentityRegistrySet& registries,
                        const VerifiedModuleGraphView& graph, const BindingTarget& target,
                        const identity::SourceSpan& span) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    auto record =
        registries.definitions().lookupRecord(value.get<DefinitionBindingTarget>().definition);
    ZC_IF_SOME(definition, record) {
      auto module = registries.modules().find(definition.module());
      ZC_IF_SOME(handle, module) { return moduleContainsSpan(graph, handle, span); }
    }
    return false;
  }
  if (value.is<SemanticImportBindingTarget>()) { return false; }
  return moduleContainsSpan(graph, value.get<ModuleBindingTarget>().module, span);
}

bool targetOwnedByModule(const identity::SemanticIdentityRegistrySet& registries,
                         const BindingTarget& target, identity::ModuleId module) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    auto record =
        registries.definitions().lookupRecord(value.get<DefinitionBindingTarget>().definition);
    ZC_IF_SOME(definition, record) {
      auto owner = registries.modules().lookup(module);
      ZC_IF_SOME(moduleKey, owner) {
        return definition.module().encode().asPtr() == moduleKey.encode().asPtr();
      }
    }
    return false;
  }
  if (value.is<SemanticImportBindingTarget>()) {
    auto owner = registries.modules().lookup(module);
    ZC_IF_SOME(moduleKey, owner) {
      return value.get<SemanticImportBindingTarget>().binding.requester().encode().asPtr() ==
             moduleKey.encode().asPtr();
    }
    return false;
  }
  return value.get<ModuleBindingTarget>().module == module;
}

bool encodeSurfaceEntry(identity::CanonicalEncoder& encoder,
                        const identity::SemanticIdentityRegistrySet& registries,
                        const VerifiedModuleGraphView& graph,
                        identity::SemanticContextBrand context, identity::ModuleId sourceModule,
                        const ExportSurfaceEntry& entry) {
  if (!moduleContainsSpan(graph, sourceModule, entry.bindingSpan) ||
      !targetContainsSpan(registries, graph, entry.canonicalTarget,
                          entry.canonicalDeclarationSpan) ||
      !targetOwnedByModule(registries, entry.bindingIdentity, sourceModule)) {
    return false;
  }
  encodeBindingName(encoder, entry.name);
  if (!encodeSurfaceTarget(encoder, registries, context, entry.bindingIdentity) ||
      !encodeSurfaceTarget(encoder, registries, context, entry.canonicalTarget)) {
    return false;
  }
  if (!encodeSurfaceVisibility(encoder, registries, context, entry.visibility)) { return false; }
  const auto external = entry.visibility.value().is<ExternalVisibility>();
  if (entry.exported != external || (entry.exportSpan != zc::none) != entry.exported) {
    return false;
  }
  if (!external) {
    const auto& moduleVisibility = entry.visibility.value().get<ModuleVisibility>();
    if (moduleVisibility.module != sourceModule) { return false; }
  }
  ZC_IF_SOME(alias, entry.aliasSpan) {
    if (!moduleContainsSpan(graph, sourceModule, alias)) { return false; }
  }
  ZC_IF_SOME(exportSpan, entry.exportSpan) {
    if (!moduleContainsSpan(graph, sourceModule, exportSpan)) { return false; }
  }
  encoder.encodeBool(entry.exported);
  entry.bindingSpan.encode(encoder);
  entry.canonicalDeclarationSpan.encode(encoder);
  encodeOptionalSpan(encoder, entry.aliasSpan);
  encodeOptionalSpan(encoder, entry.exportSpan);
  encoder.encodeSequenceSize(entry.reexportChain.size());
  zc::TreeMap<zc::String, bool> seenSteps;
  for (const auto& step : entry.reexportChain) {
    if (!step.module.belongsTo(context) ||
        !targetOwnedByModule(registries, step.bindingIdentity, step.module) ||
        !sameBindingTarget(step.canonicalTarget, entry.canonicalTarget) ||
        !moduleContainsSpan(graph, step.module, step.exportSpan)) {
      return false;
    }
    auto module = registries.modules().lookup(step.module);
    if (module == zc::none) { return false; }
    identity::CanonicalEncoder stepEncoder;
    if (!encodeSurfaceTarget(stepEncoder, registries, context, step.bindingIdentity)) {
      return false;
    }
    auto stepBytes = stepEncoder.finish();
    auto stepKey = zc::encodeHex(stepBytes.asPtr());
    if (seenSteps.find(stepKey) != zc::none) { return false; }
    seenSteps.insert(zc::mv(stepKey), true);
    encoder.encodeByteString(stepBytes.asPtr());
    ZC_IF_SOME(moduleValue, module) { moduleValue.encode(encoder); }
    if (!encodeSurfaceTarget(encoder, registries, context, step.canonicalTarget)) { return false; }
    step.exportSpan.encode(encoder);
  }
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeSurfaceEntries(
    const identity::SemanticIdentityRegistrySet& registries, identity::SemanticContextBrand context,
    const VerifiedModuleGraphView& graph, identity::ModuleId sourceModule,
    zc::ArrayPtr<const ExportSurfaceEntry> entries) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(entries.size());
  for (const auto& entry : entries) {
    encodeBindingName(encoder, entry.name);
    if (!encodeSurfaceEntry(encoder, registries, graph, context, sourceModule, entry)) {
      return zc::none;
    }
  }
  return encoder.finish();
}

enum class SurfaceValidation : uint8_t { Valid, InputMismatch, InvalidEdge, RevisionMismatch };

SurfaceValidation validateSurface(const BindingInputCandidate& candidate,
                                  identity::ModuleId expectedModule,
                                  const VerifiedExportSurface& surface) {
  const auto& registries = candidate.registries;
  if (surface.sourceModule() != expectedModule ||
      !surface.sourceModule().belongsTo(candidate.semanticContext) ||
      !surface.sourcePackage().belongsTo(candidate.semanticContext)) {
    return SurfaceValidation::InputMismatch;
  }
  auto module = registries.modules().lookup(surface.sourceModule());
  auto package = registries.packages().lookup(surface.sourcePackage());
  if (module == zc::none || package == zc::none) { return SurfaceValidation::InputMismatch; }
  ZC_IF_SOME(moduleValue, module) {
    ZC_IF_SOME(packageValue, package) {
      if (moduleValue.crate().package().encode().asPtr() != packageValue.encode().asPtr()) {
        return SurfaceValidation::InputMismatch;
      }
      auto visible =
          encodeSurfaceEntries(registries, candidate.semanticContext, candidate.moduleGraph,
                               surface.sourceModule(), surface.visibleEntries());
      auto exports =
          encodeSurfaceEntries(registries, candidate.semanticContext, candidate.moduleGraph,
                               surface.sourceModule(), surface.exports());
      if (visible == zc::none || exports == zc::none) { return SurfaceValidation::InvalidEdge; }
      size_t exportIndex = 0;
      for (const auto& entry : surface.visibleEntries()) {
        if (!entry.exported) { continue; }
        if (exportIndex >= surface.exports().size()) { return SurfaceValidation::InvalidEdge; }
        identity::CanonicalEncoder visibleEncoder;
        identity::CanonicalEncoder exportEncoder;
        if (!encodeSurfaceEntry(visibleEncoder, registries, candidate.moduleGraph,
                                candidate.semanticContext, surface.sourceModule(), entry) ||
            !encodeSurfaceEntry(exportEncoder, registries, candidate.moduleGraph,
                                candidate.semanticContext, surface.sourceModule(),
                                surface.exports()[exportIndex]) ||
            visibleEncoder.finish().asPtr() != exportEncoder.finish().asPtr()) {
          return SurfaceValidation::InvalidEdge;
        }
        ++exportIndex;
      }
      if (exportIndex != surface.exports().size()) { return SurfaceValidation::InvalidEdge; }
      ZC_IF_SOME(visibleValue, visible) {
        ZC_IF_SOME(exportsValue, exports) {
          const auto moduleBytes = moduleValue.encode();
          const auto packageBytes = packageValue.encode();
          auto revision = ExportSurfaceRevision::computeFramed(
              candidate.moduleGraph.semanticFingerprint().digest(), moduleBytes.asPtr(),
              packageBytes.asPtr(), visibleValue.asPtr(), exportsValue.asPtr());
          if (revision == zc::none) { return SurfaceValidation::InvalidEdge; }
          ZC_IF_SOME(revisionValue, revision) {
            if (revisionValue.digest() != surface.revision().digest()) {
              return SurfaceValidation::RevisionMismatch;
            }
          }
        }
      }
    }
  }
  return SurfaceValidation::Valid;
}

zc::Maybe<const FrozenDefinitionEntry&> definitionAt(
    const FrozenDefinitionInventoryView& definitions, ast::NodeId node) {
  for (const auto& entry : definitions.definitions()) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

zc::Maybe<identity::SourceSpan> explicitAliasSpan(const VerifiedParsedModule& parsedModule,
                                                  ast::NodeId syntax, ast::SyntaxKind syntaxKind,
                                                  uint32_t tokenOrdinal) {
  if (!parsedModule.tree().contains(syntax) ||
      parsedModule.tree().node(syntax).kind != syntaxKind) {
    return zc::none;
  }
  return parsedModule.retainedTokenSpan(syntax, tokenOrdinal, ast::SyntaxKind::Identifier);
}

zc::String bindingInputFailureKey(const BindingInputSourceFailure& failure) {
  identity::CanonicalEncoder encoder;
  failure.source().encode(encoder);
  encoder.encodeUint32(static_cast<uint16_t>(failure.diagnostic()));
  encoder.encodeUint32(failure.syntax().value);
  return zc::encodeHex(encoder.finish().asPtr());
}

bool sameSourceSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

zc::Maybe<zc::String> renderModuleRequestPath(const ModuleDependencyRequest& request) {
  if (request.normalizedPath().size() == 0) { return zc::none; }
  zc::String result;
  bool first = true;
  for (const auto& segment : request.normalizedPath()) {
    if (!first) { result = zc::str(result, "::"_zc); }
    result = zc::str(result, segment.text());
    first = false;
  }
  return zc::mv(result);
}

zc::Maybe<identity::DefinitionNamespace> semanticNamespace(Namespace nameSpace) {
  switch (nameSpace) {
    case Namespace::Value:
      return identity::DefinitionNamespace::Value;
    case Namespace::Type:
      return identity::DefinitionNamespace::Type;
    case Namespace::Module:
      return identity::DefinitionNamespace::Module;
    case Namespace::Label:
    case Namespace::Attribute:
      return zc::none;
  }
  return zc::none;
}

identity::SemanticImportOperation semanticImportOperation(ImportBindingKind kind) {
  return kind == ImportBindingKind::Import ? identity::SemanticImportOperation::Import
                                           : identity::SemanticImportOperation::ForeignReexport;
}

zc::String resolvedImportOrderKey(const identity::SemanticImportBindingKey& binding,
                                  uint32_t schemaPreorderOrdinal, ast::NodeId syntax) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint64(schemaPreorderOrdinal);
  encoder.encodeUint64(syntax.value);
  encoder.encodeUint8(static_cast<uint8_t>(binding.localNamespace()));
  encoder.encodeByteString(binding.encode().asPtr());
  return zc::encodeHex(encoder.finish().asPtr());
}

zc::Vector<ReexportProvenanceStep> cloneReexportChain(
    zc::ArrayPtr<const ReexportProvenanceStep> chain) {
  zc::Vector<ReexportProvenanceStep> result(chain.size());
  for (const auto& step : chain) { result.add(step.clone()); }
  return result;
}

}  // namespace

zc::Maybe<ModuleGraphRevision> computeModuleGraphRevision(
    const identity::SemanticContextFingerprint& fingerprint,
    zc::ArrayPtr<const identity::ModuleKey> modules,
    zc::ArrayPtr<const VerifiedModuleDependencyEdge> edges) {
  zc::Vector<uint8_t> bytes;
  for (size_t index = 0; index < sizeof(kGraphDomain) - 1; ++index) {
    bytes.add(static_cast<uint8_t>(kGraphDomain[index]));
  }
  bytes.add(0x00);
  bytes.addAll(fingerprint.digest().bytes());
  appendUint64(bytes, modules.size());
  zc::Maybe<zc::Array<uint8_t>> previous;
  for (const auto& module : modules) {
    auto moduleBytes = module.encode();
    ZC_IF_SOME(previousBytes, previous) {
      if (!(previousBytes.asPtr() < moduleBytes.asPtr())) { return zc::none; }
    }
    appendUint64(bytes, moduleBytes.size());
    bytes.addAll(moduleBytes.asPtr());
    previous = zc::mv(moduleBytes);
  }
  appendUint64(bytes, edges.size());
  zc::ArrayPtr<const uint8_t> previousEdge;
  for (const auto& edge : edges) {
    if (previousEdge.size() != 0 && !(previousEdge < edge.encodedKey())) { return zc::none; }
    appendUint64(bytes, edge.encodedKey().size());
    bytes.addAll(edge.encodedKey());
    previousEdge = edge.encodedKey();
  }
  auto digest = identity::sha256(bytes.asPtr());
  ZC_IF_SOME(value, digest) { return ModuleGraphRevision(value); }
  return zc::none;
}

ModuleGraphRevision::ModuleGraphRevision(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}
const identity::Sha256Digest& ModuleGraphRevision::digest() const noexcept { return value; }

ModuleGraphModule::ModuleGraphModule(identity::ModuleKey&& key, identity::ModuleId module) noexcept
    : keyValue(zc::mv(key)), moduleValue(module) {}
const identity::ModuleKey& ModuleGraphModule::key() const noexcept { return keyValue; }
identity::ModuleId ModuleGraphModule::module() const noexcept { return moduleValue; }

VerifiedModuleDependencyEdge::VerifiedModuleDependencyEdge(ModuleDependencyRequest&& request,
                                                           identity::ModuleId target,
                                                           zc::Array<uint8_t>&& encodedKey) noexcept
    : requestValue(zc::mv(request)), targetValue(target), encodedKeyValue(zc::mv(encodedKey)) {}
const ModuleDependencyRequest& VerifiedModuleDependencyEdge::request() const noexcept {
  return requestValue;
}
identity::ModuleId VerifiedModuleDependencyEdge::target() const noexcept { return targetValue; }
zc::ArrayPtr<const uint8_t> VerifiedModuleDependencyEdge::encodedKey() const noexcept {
  return encodedKeyValue.asPtr();
}

ModuleGraphSourceFailure::ModuleGraphSourceFailure(
    ModuleGraphDiagnostic diagnostic, ModuleDependencyRequest&& request,
    zc::Vector<identity::ModuleKey>&& candidates) noexcept
    : diagnosticValue(diagnostic),
      requestValue(zc::mv(request)),
      candidateValues(zc::mv(candidates)) {}
ModuleGraphDiagnostic ModuleGraphSourceFailure::diagnostic() const noexcept {
  return diagnosticValue;
}
const ModuleDependencyRequest& ModuleGraphSourceFailure::request() const noexcept {
  return requestValue;
}
zc::ArrayPtr<const identity::ModuleKey> ModuleGraphSourceFailure::candidates() const noexcept {
  return candidateValues.asPtr();
}
ModuleGraphSourceRejected::ModuleGraphSourceRejected(
    zc::Vector<ModuleGraphSourceFailure>&& failures) noexcept
    : failureValues(zc::mv(failures)) {}
zc::ArrayPtr<const ModuleGraphSourceFailure> ModuleGraphSourceRejected::failures() const noexcept {
  return failureValues.asPtr();
}

struct VerifiedModuleGraphView::Impl final {
  Impl(identity::SemanticContextBrand context, identity::SemanticContextFingerprint&& fingerprint,
       identity::ModuleId requester, zc::Vector<identity::ModuleKey>&& modules,
       zc::Vector<identity::ModuleId>&& handles, zc::Vector<identity::SourceFileKey>&& sources,
       zc::Vector<VerifiedModuleDependencyEdge>&& edges, ModuleGraphRevision revision)
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        requester(requester),
        modules(zc::mv(modules)),
        handles(zc::mv(handles)),
        sources(zc::mv(sources)),
        edges(zc::mv(edges)),
        revision(revision) {}

  identity::SemanticContextBrand context;
  identity::SemanticContextFingerprint fingerprint;
  identity::ModuleId requester;
  zc::Vector<identity::ModuleKey> modules;
  zc::Vector<identity::ModuleId> handles;
  zc::Vector<identity::SourceFileKey> sources;
  zc::Vector<VerifiedModuleDependencyEdge> edges;
  ModuleGraphRevision revision;
};

VerifiedModuleGraphView::VerifiedModuleGraphView(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedModuleGraphView::~VerifiedModuleGraphView() noexcept(false) = default;
VerifiedModuleGraphView::VerifiedModuleGraphView(VerifiedModuleGraphView&&) noexcept = default;
VerifiedModuleGraphView& VerifiedModuleGraphView::operator=(VerifiedModuleGraphView&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedModuleGraphView::semanticContext() const noexcept {
  return impl->context;
}
const identity::SemanticContextFingerprint& VerifiedModuleGraphView::semanticFingerprint()
    const noexcept {
  return impl->fingerprint;
}
identity::ModuleId VerifiedModuleGraphView::requester() const noexcept { return impl->requester; }
const ModuleGraphRevision& VerifiedModuleGraphView::revision() const noexcept {
  return impl->revision;
}
zc::ArrayPtr<const identity::ModuleKey> VerifiedModuleGraphView::modules() const noexcept {
  return impl->modules.asPtr();
}
zc::ArrayPtr<const VerifiedModuleDependencyEdge> VerifiedModuleGraphView::edges() const noexcept {
  return impl->edges.asPtr();
}
zc::Maybe<const identity::SourceFileKey&> VerifiedModuleGraphView::sourceFile(
    identity::ModuleId module) const noexcept {
  for (size_t index = 0; index < impl->handles.size(); ++index) {
    if (impl->handles[index] == module) return impl->sources[index];
  }
  return zc::none;
}

struct VerifiedModuleGraph::Impl final {
  Impl(identity::SemanticContextBrand context, identity::SemanticContextFingerprint&& fingerprint,
       zc::Vector<identity::ModuleKey>&& modules, zc::Vector<identity::ModuleId>&& handles,
       zc::Vector<identity::SourceFileKey>&& sources,
       zc::Vector<VerifiedModuleDependencyEdge>&& edges,
       zc::Vector<identity::RequesterModuleAncestry>&& requesterAncestry,
       zc::Vector<identity::ModuleCatalogPathBucket>&& catalogBuckets, ModuleGraphRevision revision)
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        modules(zc::mv(modules)),
        handles(zc::mv(handles)),
        sources(zc::mv(sources)),
        edges(zc::mv(edges)),
        requesterAncestry(zc::mv(requesterAncestry)),
        catalogBuckets(zc::mv(catalogBuckets)),
        revision(revision) {}

  identity::SemanticContextBrand context;
  identity::SemanticContextFingerprint fingerprint;
  zc::Vector<identity::ModuleKey> modules;
  zc::Vector<identity::ModuleId> handles;
  zc::Vector<identity::SourceFileKey> sources;
  zc::Vector<VerifiedModuleDependencyEdge> edges;
  zc::Vector<identity::RequesterModuleAncestry> requesterAncestry;
  zc::Vector<identity::ModuleCatalogPathBucket> catalogBuckets;
  ModuleGraphRevision revision;
};

VerifiedModuleGraph::VerifiedModuleGraph(zc::Own<Impl>&& graphImpl) noexcept
    : impl(zc::mv(graphImpl)) {}
VerifiedModuleGraph::~VerifiedModuleGraph() noexcept(false) = default;
VerifiedModuleGraph::VerifiedModuleGraph(VerifiedModuleGraph&&) noexcept = default;
VerifiedModuleGraph& VerifiedModuleGraph::operator=(VerifiedModuleGraph&&) noexcept = default;
identity::SemanticContextBrand VerifiedModuleGraph::semanticContext() const noexcept {
  return impl->context;
}
const ModuleGraphRevision& VerifiedModuleGraph::revision() const noexcept { return impl->revision; }
zc::ArrayPtr<const identity::ModuleKey> VerifiedModuleGraph::modules() const noexcept {
  return impl->modules.asPtr();
}
zc::ArrayPtr<const VerifiedModuleDependencyEdge> VerifiedModuleGraph::edges() const noexcept {
  return impl->edges.asPtr();
}
zc::ArrayPtr<const identity::RequesterModuleAncestry> VerifiedModuleGraph::requesterAncestryInputs()
    const noexcept {
  return impl->requesterAncestry.asPtr();
}
zc::ArrayPtr<const identity::ModuleCatalogPathBucket> VerifiedModuleGraph::catalogPathBucketInputs()
    const noexcept {
  return impl->catalogBuckets.asPtr();
}
zc::Maybe<const identity::SourceFileKey&> VerifiedModuleGraph::sourceFile(
    identity::ModuleId module) const noexcept {
  if (!module.belongsTo(impl->context)) { return zc::none; }
  for (size_t index = 0; index < impl->handles.size(); ++index) {
    if (impl->handles[index] == module) { return impl->sources[index]; }
  }
  return zc::none;
}
zc::Maybe<VerifiedModuleGraphView> VerifiedModuleGraph::view(identity::ModuleId requester) const {
  bool found = false;
  for (const auto handle : impl->handles) {
    if (handle == requester) {
      found = true;
      break;
    }
  }
  if (!found || !requester.belongsTo(impl->context)) { return zc::none; }
  zc::Vector<identity::ModuleKey> modules(impl->modules.size());
  for (const auto& module : impl->modules) { modules.add(module.clone()); }
  zc::Vector<identity::ModuleId> handles(impl->handles.size());
  for (const auto handle : impl->handles) { handles.add(handle); }
  zc::Vector<identity::SourceFileKey> sources(impl->sources.size());
  for (const auto& source : impl->sources) { sources.add(source.clone()); }
  zc::Vector<VerifiedModuleDependencyEdge> edges(impl->edges.size());
  for (const auto& edge : impl->edges) {
    edges.add(VerifiedModuleDependencyEdge(edge.request().clone(), edge.target(),
                                           zc::heapArray(edge.encodedKey())));
  }
  return VerifiedModuleGraphView(zc::heap<VerifiedModuleGraphView::Impl>(
      impl->context, impl->fingerprint.clone(), requester, zc::mv(modules), zc::mv(handles),
      zc::mv(sources), zc::mv(edges), impl->revision));
}

ModuleGraphVerificationResult ModuleGraphVerifier::verify(const ModuleGraphCandidate& candidate) {
  const auto& registries = candidate.registries;
  if (!candidate.semanticContext.isValid() || !allRegistriesFrozen(registries) ||
      candidate.modules.size() == 0 || candidate.modules.size() != registries.modules().size() ||
      candidate.parsedModules.size() != candidate.modules.size() ||
      candidate.resolver.catalog().size() != candidate.modules.size()) {
    return failure(ModuleGraphInvariantKind::InputMismatch);
  }
  auto fingerprint = identity::SemanticContextFingerprint::compute(
      registries, candidate.packageEdges, candidate.crateEdges);
  if (fingerprint == zc::none) { return failure(ModuleGraphInvariantKind::InputMismatch); }
  ZC_IF_SOME(fingerprintValue, fingerprint) {
    if (fingerprintValue.digest() != candidate.semanticContextFingerprint.digest()) {
      return failure(ModuleGraphInvariantKind::RevisionMismatch);
    }
    zc::TreeMap<zc::String, ModuleGraphModule> canonicalModules;
    for (const auto& module : candidate.modules) {
      if (!module.module().belongsTo(candidate.semanticContext) ||
          registries.modules().validate(module.module()) != identity::FrozenRegistryFailure::None) {
        return failure(ModuleGraphInvariantKind::InputMismatch, module.module());
      }
      auto registered = registries.modules().lookup(module.module());
      if (registered == zc::none) {
        return failure(ModuleGraphInvariantKind::InputMismatch, module.module());
      }
      ZC_IF_SOME(registeredValue, registered) {
        const auto encoded = module.key().encode();
        if (encoded.asPtr() != registeredValue.encode().asPtr()) {
          return failure(ModuleGraphInvariantKind::InputMismatch, module.module());
        }
        auto sortKey = zc::encodeHex(encoded.asPtr());
        if (canonicalModules.find(sortKey) != zc::none) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, module.module());
        }
        canonicalModules.insert(zc::mv(sortKey),
                                ModuleGraphModule(module.key().clone(), module.module()));
      }
    }
    zc::Vector<identity::ModuleKey> verifiedModules(canonicalModules.size());
    zc::Vector<identity::ModuleId> verifiedHandles(canonicalModules.size());
    for (auto& entry : canonicalModules) {
      verifiedModules.add(entry.value.key().clone());
      verifiedHandles.add(entry.value.module());
    }
    for (const auto& entry : candidate.resolver.catalog()) {
      auto found = canonicalModules.find(zc::encodeHex(entry.key.encode().asPtr()));
      if (found == zc::none) { return failure(ModuleGraphInvariantKind::InputMismatch); }
      ZC_IF_SOME(value, found) {
        if (value.module() != entry.module) {
          return failure(ModuleGraphInvariantKind::InputMismatch, entry.module);
        }
      }
    }
    zc::Vector<identity::SourceFileKey> verifiedSources(verifiedHandles.size());
    for (size_t moduleIndex = 0; moduleIndex < verifiedHandles.size(); ++moduleIndex) {
      zc::Maybe<const StructuralModuleCatalogEntry&> matched;
      for (const auto& entry : candidate.resolver.catalog()) {
        if (entry.module != verifiedHandles[moduleIndex]) continue;
        if (matched != zc::none) {
          return failure(ModuleGraphInvariantKind::InputMismatch, entry.module);
        }
        matched = entry;
      }
      if (matched == zc::none) {
        return failure(ModuleGraphInvariantKind::InputMismatch, verifiedHandles[moduleIndex]);
      }
      ZC_IF_SOME(entry, matched) {
        if (registries.sourceFiles().find(entry.source) == zc::none ||
            !entry.source.belongsTo(verifiedModules[moduleIndex].crate())) {
          return failure(ModuleGraphInvariantKind::InputMismatch, verifiedHandles[moduleIndex]);
        }
        verifiedSources.add(entry.source.clone());
      }
    }

    zc::TreeMap<zc::String, ModuleDependencyRequest> expectedRequests;
    zc::TreeMap<zc::String, uint8_t> parsedModuleCensus;
    for (const auto& parsed : candidate.parsedModules) {
      zc::Maybe<identity::ModuleKey> module;
      for (const auto& entry : candidate.modules) {
        if (entry.module() == parsed.module) {
          if (module != zc::none) {
            return failure(ModuleGraphInvariantKind::InputMismatch, parsed.module);
          }
          module = entry.key().clone();
        }
      }
      if (module == zc::none ||
          !parsed.parsedModule.sourceFile().belongsTo(candidate.semanticContext)) {
        return failure(ModuleGraphInvariantKind::InputMismatch, parsed.module);
      }
      ZC_IF_SOME(moduleValue, module) {
        auto censusKey = zc::encodeHex(moduleValue.encode().asPtr());
        if (parsedModuleCensus.find(censusKey) != zc::none) {
          return failure(ModuleGraphInvariantKind::InputMismatch, parsed.module);
        }
        parsedModuleCensus.insert(zc::mv(censusKey), uint8_t{1});
      }
      const auto& tree = parsed.parsedModule.tree();
      if (!tree.contains(tree.root()) ||
          tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
        return failure(ModuleGraphInvariantKind::InputMismatch, parsed.module);
      }
      auto source = registries.sourceFiles().lookup(parsed.parsedModule.sourceFile());
      auto snapshot = registries.sourceSnapshot(parsed.parsedModule.sourceFile());
      if (source == zc::none || snapshot == zc::none) {
        return failure(ModuleGraphInvariantKind::InputMismatch, parsed.module);
      }
      ZC_IF_SOME(moduleValue, module) {
        ZC_IF_SOME(sourceValue, source) {
          if (!parsed.parsedModule.source().sameAs(sourceValue) ||
              !sourceValue.belongsTo(moduleValue.crate())) {
            return failure(ModuleGraphInvariantKind::InputMismatch, parsed.module);
          }
        }
      }
      ZC_IF_SOME(snapshotValue, snapshot) {
        if (snapshotValue.contentDigest() != parsed.parsedModule.contentDigest() ||
            snapshotValue.bytes().size() != parsed.parsedModule.byteLength()) {
          return failure(ModuleGraphInvariantKind::InputMismatch, parsed.module);
        }
      }
      auto ordinals = schemaPreorderOrdinals(tree);
      if (ordinals == zc::none) {
        return failure(ModuleGraphInvariantKind::InputMismatch, parsed.module);
      }
      bool requestValid = true;
      ZC_IF_SOME(schemaOrdinals, ordinals) {
        ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
          identity::ModuleDependencyKind kind = identity::ModuleDependencyKind::Import;
          ast::NodeId path;
          if (syntax.kind == ast::SyntaxKind::ImportDeclaration) {
            path = ast::NodeId(syntax.payload.words[ast::kImportDeclarationPathWord]);
          } else if (syntax.kind == ast::SyntaxKind::ExportDeclaration) {
            path = ast::NodeId(syntax.payload.words[ast::kExportDeclarationPathWord]);
            if (!tree.contains(path)) { return; }
            kind = identity::ModuleDependencyKind::ForeignReexport;
          } else if (syntax.kind == ast::SyntaxKind::ModuleDeclaration &&
                     static_cast<ast::ModuleDeclarationForm>(
                         syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
                         ast::ModuleDeclarationForm::Alias) {
            path = ast::NodeId(syntax.payload.words[ast::kModuleDeclarationAliasTargetWord]);
            kind = identity::ModuleDependencyKind::ModuleAlias;
          } else {
            return;
          }
          auto normalized = normalizedModulePath(tree, path);
          auto span = parsed.parsedModule.spanFor(syntax.range);
          if (normalized == zc::none || span == zc::none || node.value >= schemaOrdinals.size() ||
              schemaOrdinals[node.value] == UINT32_MAX) {
            requestValid = false;
            return;
          }
          ZC_IF_SOME(pathValue, normalized) {
            ZC_IF_SOME(spanValue, span) {
              auto resolutionKey =
                  candidate.resolver.resolutionKey(parsed.module, kind, zc::mv(pathValue));
              if (resolutionKey == zc::none) {
                requestValid = false;
                return;
              }
              zc::Vector<ModuleSyntaxDependencySite> sites;
              sites.add(
                  ModuleSyntaxDependencySite(node, zc::mv(spanValue), schemaOrdinals[node.value]));
              zc::Maybe<ModuleDependencyRequest> request;
              ZC_IF_SOME(keyValue, resolutionKey) {
                request = ModuleDependencyRequest::source(parsed.module, zc::mv(keyValue),
                                                          candidate.resolver.environmentRevision(),
                                                          zc::mv(sites));
              }
              if (request == zc::none) {
                requestValid = false;
                return;
              }
              ZC_IF_SOME(requestValue, request) {
                auto sortKey = zc::encodeHex(requestValue.key().encode().asPtr());
                ZC_IF_SOME(existing, expectedRequests.find(sortKey)) {
                  zc::Vector<ModuleSyntaxDependencySite> combinedSites;
                  for (const auto& site : existing.syntaxSites()) {
                    combinedSites.add(ModuleSyntaxDependencySite(site.node, site.span.clone(),
                                                                 site.schemaPreorderOrdinal));
                  }
                  for (const auto& site : requestValue.syntaxSites()) {
                    combinedSites.add(ModuleSyntaxDependencySite(site.node, site.span.clone(),
                                                                 site.schemaPreorderOrdinal));
                  }
                  auto combined = ModuleDependencyRequest::source(
                      parsed.module, existing.key().clone(),
                      candidate.resolver.environmentRevision(), zc::mv(combinedSites));
                  if (combined == zc::none) {
                    requestValid = false;
                    return;
                  }
                  ZC_IF_SOME(combinedValue, combined) {
                    existing = zc::mv(combinedValue);
                    return;
                  }
                  requestValid = false;
                  return;
                }
                expectedRequests.insert(zc::mv(sortKey), zc::mv(requestValue));
              }
            }
          }
        });
      }
      if (!requestValid) {
        return failure(ModuleGraphInvariantKind::IncompleteResolution, parsed.module);
      }
    }
    if (parsedModuleCensus.size() != canonicalModules.size()) {
      return failure(ModuleGraphInvariantKind::InputMismatch);
    }

    for (const auto& prelude : candidate.configuredPreludes) {
      if (!prelude.isPrelude() ||
          prelude.environmentRevision() != candidate.resolver.environmentRevision()) {
        return failure(ModuleGraphInvariantKind::InvalidPrelude, prelude.requester());
      }
      auto sortKey = zc::encodeHex(prelude.key().encode().asPtr());
      if (expectedRequests.find(sortKey) != zc::none) {
        return failure(ModuleGraphInvariantKind::InvalidPrelude, prelude.requester());
      }
      expectedRequests.insert(zc::mv(sortKey), prelude.clone());
    }
    if (expectedRequests.size() != candidate.resolutions.size()) {
      return failure(ModuleGraphInvariantKind::IncompleteResolution);
    }

    zc::TreeMap<zc::String, size_t> providedResolutions;
    for (size_t resolutionIndex = 0; resolutionIndex < candidate.resolutions.size();
         ++resolutionIndex) {
      const auto& resolution = candidate.resolutions[resolutionIndex];
      const auto& request = resolutionRequest(resolution);
      const auto& receipt = resolutionReceipt(resolution);
      if (!candidate.resolver.verifiesReceipt(request, receipt)) {
        return failure(ModuleGraphInvariantKind::InvalidEdge, request.requester());
      }
      auto sortKey = zc::encodeHex(request.key().encode().asPtr());
      if (providedResolutions.find(sortKey) != zc::none ||
          expectedRequests.find(sortKey) == zc::none) {
        return failure(ModuleGraphInvariantKind::IncompleteResolution, request.requester());
      }
      providedResolutions.insert(zc::mv(sortKey), resolutionIndex);
    }

    zc::TreeMap<zc::String, VerifiedModuleDependencyEdge> canonicalEdges;
    zc::TreeMap<zc::String, ModuleGraphSourceFailure> sourceFailures;
    for (const auto& expected : expectedRequests) {
      auto provided = providedResolutions.find(expected.key);
      if (provided == zc::none) {
        return failure(ModuleGraphInvariantKind::IncompleteResolution, expected.value.requester());
      }
      ZC_IF_SOME(resolutionIndex, provided) {
        const auto& resolution = candidate.resolutions[resolutionIndex];
        const auto& request = resolutionRequest(resolution);
        if (resolution.is<MissingModulePath>()) {
          if (request.isPrelude()) {
            return failure(ModuleGraphInvariantKind::InvalidPrelude, request.requester());
          }
          auto key = zc::str(expected.key, ":missing"_zc);
          sourceFailures.insert(zc::mv(key), ModuleGraphSourceFailure(
                                                 missingDiagnostic(request.kind()), request.clone(),
                                                 zc::Vector<identity::ModuleKey>()));
          continue;
        }
        if (resolution.is<AmbiguousModulePath>()) {
          if (request.isPrelude()) {
            return failure(ModuleGraphInvariantKind::InvalidPrelude, request.requester());
          }
          const auto& ambiguous = resolution.get<AmbiguousModulePath>();
          if (ambiguous.candidates.size() < 2 ||
              ambiguous.candidates.size() != ambiguous.receipt.candidates().size()) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, request.requester());
          }
          for (size_t index = 0; index < ambiguous.candidates.size(); ++index) {
            if (ambiguous.candidates[index].encode().asPtr() !=
                ambiguous.receipt.candidates()[index].encode().asPtr()) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, request.requester());
            }
          }
          auto key = zc::str(expected.key, ":ambiguous"_zc);
          sourceFailures.insert(
              zc::mv(key),
              ModuleGraphSourceFailure(ambiguousDiagnostic(request.kind()), request.clone(),
                                       cloneModuleKeys(ambiguous.candidates.asPtr())));
          continue;
        }
        const auto& resolved = resolution.get<ResolvedModulePath>();
        if (resolved.receipt.candidates().size() != 1) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, request.requester());
        }
        auto target = candidate.resolver.moduleForKey(resolved.receipt.candidates()[0]);
        if (target == zc::none) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, request.requester());
        }
        identity::ModuleId targetValue;
        ZC_IF_SOME(value, target) { targetValue = value; }
        if (targetValue != resolved.target) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, request.requester());
        }
        auto requesterKey = registries.modules().lookup(request.requester());
        auto targetKey = registries.modules().lookup(targetValue);
        if (requesterKey == zc::none || targetKey == zc::none) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, request.requester());
        }
        ZC_IF_SOME(requesterValue, requesterKey) {
          ZC_IF_SOME(targetKeyValue, targetKey) {
            auto edgeKey = encodeDependencyEdgeKey(requesterValue, request, targetKeyValue);
            auto sortKey = zc::encodeHex(edgeKey.asPtr());
            if (canonicalEdges.find(sortKey) != zc::none) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, request.requester());
            }
            canonicalEdges.insert(
                zc::mv(sortKey),
                VerifiedModuleDependencyEdge(request.clone(), targetValue, zc::mv(edgeKey)));
          }
        }
      }
    }

    zc::Vector<VerifiedModuleDependencyEdge> verifiedEdges(canonicalEdges.size());
    for (auto& edge : canonicalEdges) { verifiedEdges.add(zc::mv(edge.value)); }

    const size_t moduleCount = verifiedHandles.size();
    zc::Vector<uint8_t> reachability(moduleCount * moduleCount);
    reachability.resize(moduleCount * moduleCount);
    for (auto& value : reachability) { value = 0; }
    for (const auto& edge : verifiedEdges) {
      size_t requesterIndex = moduleCount;
      size_t targetIndex = moduleCount;
      for (size_t index = 0; index < moduleCount; ++index) {
        if (verifiedHandles[index] == edge.request().requester()) { requesterIndex = index; }
        if (verifiedHandles[index] == edge.target()) { targetIndex = index; }
      }
      if (requesterIndex == moduleCount || targetIndex == moduleCount) {
        return failure(ModuleGraphInvariantKind::InvalidEdge, edge.request().requester());
      }
      reachability[requesterIndex * moduleCount + targetIndex] = 1;
    }
    for (size_t through = 0; through < moduleCount; ++through) {
      for (size_t from = 0; from < moduleCount; ++from) {
        for (size_t to = 0; to < moduleCount; ++to) {
          if (reachability[from * moduleCount + through] != 0 &&
              reachability[through * moduleCount + to] != 0) {
            reachability[from * moduleCount + to] = 1;
          }
        }
      }
    }
    zc::Vector<uint8_t> assigned(moduleCount);
    assigned.resize(moduleCount);
    for (auto& value : assigned) { value = 0; }
    for (size_t seed = 0; seed < moduleCount; ++seed) {
      if (assigned[seed] != 0) { continue; }
      zc::Vector<size_t> component;
      component.add(seed);
      assigned[seed] = 1;
      for (size_t other = seed + 1; other < moduleCount; ++other) {
        if (reachability[seed * moduleCount + other] != 0 &&
            reachability[other * moduleCount + seed] != 0) {
          component.add(other);
          assigned[other] = 1;
        }
      }
      if (component.size() == 1 && reachability[seed * moduleCount + seed] == 0) { continue; }
      zc::Maybe<size_t> primaryIndex;
      bool hasPrelude = false;
      for (size_t edgeIndex = 0; edgeIndex < verifiedEdges.size(); ++edgeIndex) {
        const auto& edge = verifiedEdges[edgeIndex];
        bool requesterInside = false;
        bool targetInside = false;
        for (const auto index : component) {
          requesterInside = requesterInside || verifiedHandles[index] == edge.request().requester();
          targetInside = targetInside || verifiedHandles[index] == edge.target();
        }
        if (!requesterInside || !targetInside) { continue; }
        hasPrelude = hasPrelude || edge.request().kind() == identity::ModuleDependencyKind::Prelude;
        const bool edgeIsReexport =
            edge.request().kind() == identity::ModuleDependencyKind::ForeignReexport;
        bool replacePrimary = primaryIndex == zc::none;
        ZC_IF_SOME(index, primaryIndex) {
          const auto& primary = verifiedEdges[index];
          const bool primaryIsReexport =
              primary.request().kind() == identity::ModuleDependencyKind::ForeignReexport;
          replacePrimary =
              (edgeIsReexport && !primaryIsReexport) ||
              (edgeIsReexport == primaryIsReexport && edge.encodedKey() < primary.encodedKey());
        }
        if (replacePrimary) { primaryIndex = edgeIndex; }
      }
      if (primaryIndex == zc::none || hasPrelude) {
        return failure(ModuleGraphInvariantKind::InvalidPrelude);
      }
      size_t selectedIndex = 0;
      ZC_IF_SOME(value, primaryIndex) { selectedIndex = value; }
      const auto& primary = verifiedEdges[selectedIndex];
      zc::Vector<identity::ModuleKey> cycleModules(component.size());
      for (const auto index : component) { cycleModules.add(verifiedModules[index].clone()); }
      const auto diagnostic =
          primary.request().kind() == identity::ModuleDependencyKind::ForeignReexport
              ? ModuleGraphDiagnostic::CircularReexport
              : ModuleGraphDiagnostic::CircularImport;
      sourceFailures.insert(
          zc::str(zc::encodeHex(primary.encodedKey()), ":cycle"_zc),
          ModuleGraphSourceFailure(diagnostic, primary.request().clone(), zc::mv(cycleModules)));
    }

    if (sourceFailures.size() != 0) {
      zc::Vector<ModuleGraphSourceFailure> failures(sourceFailures.size());
      for (auto& entry : sourceFailures) { failures.add(zc::mv(entry.value)); }
      return ModuleGraphSourceRejected(zc::mv(failures));
    }

    auto revision = computeModuleGraphRevision(fingerprintValue, verifiedModules.asPtr(),
                                               verifiedEdges.asPtr());
    ZC_IF_SOME(revisionValue, revision) {
      zc::Vector<identity::RequesterModuleAncestry> requesterAncestry(
          candidate.resolver.requesterAncestryInputs().size());
      for (const auto& input : candidate.resolver.requesterAncestryInputs()) {
        requesterAncestry.add(input.clone());
      }
      zc::Vector<identity::ModuleCatalogPathBucket> catalogBuckets(
          candidate.resolver.catalogPathBucketInputs().size());
      for (const auto& input : candidate.resolver.catalogPathBucketInputs()) {
        catalogBuckets.add(input.clone());
      }
      return VerifiedModuleGraph(zc::heap<VerifiedModuleGraph::Impl>(
          candidate.semanticContext, zc::mv(fingerprintValue), zc::mv(verifiedModules),
          zc::mv(verifiedHandles), zc::mv(verifiedSources), zc::mv(verifiedEdges),
          zc::mv(requesterAncestry), zc::mv(catalogBuckets), revisionValue));
    }
  }
  return failure(ModuleGraphInvariantKind::RevisionMismatch);
}

void emitModuleGraphInvariant(diagnostics::DiagnosticEngine& diagnostics,
                              const ModuleGraphInvariantFact& fact) {
  diagnostics.diagnose<diagnostics::DiagID::ModuleGraphInvariant>(source::SourceLoc(),
                                                                  zc::str(fact.occurrence));
}

BindingInputSourceFailure::BindingInputSourceFailure(
    BindingInputDiagnostic diagnostic, ast::NodeId syntax, identity::SourceSpan&& source,
    zc::String&& modulePath, identity::SemanticIdentifier&& memberName) noexcept
    : diagnosticValue(diagnostic),
      syntaxValue(syntax),
      sourceValue(zc::mv(source)),
      modulePathValue(zc::mv(modulePath)),
      memberNameValue(zc::mv(memberName)) {}
BindingInputDiagnostic BindingInputSourceFailure::diagnostic() const noexcept {
  return diagnosticValue;
}
ast::NodeId BindingInputSourceFailure::syntax() const noexcept { return syntaxValue; }
const identity::SourceSpan& BindingInputSourceFailure::source() const noexcept {
  return sourceValue;
}
zc::StringPtr BindingInputSourceFailure::modulePath() const noexcept { return modulePathValue; }
const identity::SemanticIdentifier& BindingInputSourceFailure::memberName() const noexcept {
  return memberNameValue;
}

BindingInputSourceRejected::BindingInputSourceRejected(
    zc::Vector<BindingInputSourceFailure>&& failures) noexcept
    : failureValues(zc::mv(failures)) {}
zc::ArrayPtr<const BindingInputSourceFailure> BindingInputSourceRejected::failures()
    const noexcept {
  return failureValues.asPtr();
}

struct VerifiedExportSurfaceView::Impl final {
  Impl(identity::ModuleId requester, identity::ModuleId sourceModule,
       ExportSurfaceRevision sourceRevision, zc::Vector<ExportSurfaceEntry>&& visibleEntries)
      : requester(requester),
        sourceModule(sourceModule),
        sourceRevision(sourceRevision),
        visibleEntries(zc::mv(visibleEntries)) {}

  identity::ModuleId requester;
  identity::ModuleId sourceModule;
  ExportSurfaceRevision sourceRevision;
  zc::Vector<ExportSurfaceEntry> visibleEntries;
};

VerifiedExportSurfaceView::VerifiedExportSurfaceView(zc::Own<Impl>&& viewImpl) noexcept
    : impl(zc::mv(viewImpl)) {}
VerifiedExportSurfaceView::~VerifiedExportSurfaceView() noexcept(false) = default;
VerifiedExportSurfaceView::VerifiedExportSurfaceView(VerifiedExportSurfaceView&&) noexcept =
    default;
VerifiedExportSurfaceView& VerifiedExportSurfaceView::operator=(
    VerifiedExportSurfaceView&&) noexcept = default;
identity::ModuleId VerifiedExportSurfaceView::requester() const noexcept { return impl->requester; }
identity::ModuleId VerifiedExportSurfaceView::sourceModule() const noexcept {
  return impl->sourceModule;
}
const ExportSurfaceRevision& VerifiedExportSurfaceView::sourceRevision() const noexcept {
  return impl->sourceRevision;
}
zc::ArrayPtr<const ExportSurfaceEntry> VerifiedExportSurfaceView::visibleEntries() const noexcept {
  return impl->visibleEntries.asPtr();
}

ResolvedImportEdge::ResolvedImportEdge(
    identity::ModuleId requester, ast::NodeId syntax, uint32_t schemaPreorderOrdinal,
    ImportBindingKind kind, identity::ModuleId sourceModule, ExportSurfaceRevision sourceRevision,
    zc::Maybe<BindingNameKey>&& requestedName, BindingNameKey&& localName,
    identity::SemanticImportBindingKey&& binding, BindingTarget&& canonicalTarget,
    identity::SourceSpan&& declarationSpan, zc::Maybe<identity::SourceSpan>&& aliasSpan,
    identity::SourceSpan&& canonicalDeclarationSpan, zc::Maybe<identity::SourceSpan>&& exportSpan,
    zc::Vector<ReexportProvenanceStep>&& sourceReexportChain) noexcept
    : requesterValue(requester),
      syntaxValue(syntax),
      schemaPreorderOrdinalValue(schemaPreorderOrdinal),
      kindValue(kind),
      sourceModuleValue(sourceModule),
      sourceRevisionValue(sourceRevision),
      requestedNameValue(zc::mv(requestedName)),
      localNameValue(zc::mv(localName)),
      bindingValue(zc::mv(binding)),
      canonicalTargetValue(zc::mv(canonicalTarget)),
      declarationSpanValue(zc::mv(declarationSpan)),
      aliasSpanValue(zc::mv(aliasSpan)),
      canonicalDeclarationSpanValue(zc::mv(canonicalDeclarationSpan)),
      exportSpanValue(zc::mv(exportSpan)),
      sourceReexportChainValue(zc::mv(sourceReexportChain)) {}
identity::ModuleId ResolvedImportEdge::requester() const noexcept { return requesterValue; }
ast::NodeId ResolvedImportEdge::syntax() const noexcept { return syntaxValue; }
uint32_t ResolvedImportEdge::schemaPreorderOrdinal() const noexcept {
  return schemaPreorderOrdinalValue;
}
ImportBindingKind ResolvedImportEdge::kind() const noexcept { return kindValue; }
identity::ModuleId ResolvedImportEdge::sourceModule() const noexcept { return sourceModuleValue; }
const ExportSurfaceRevision& ResolvedImportEdge::sourceRevision() const noexcept {
  return sourceRevisionValue;
}
zc::Maybe<const BindingNameKey&> ResolvedImportEdge::requestedName() const noexcept {
  ZC_IF_SOME(value, requestedNameValue) { return value; }
  return zc::none;
}
const BindingNameKey& ResolvedImportEdge::localName() const noexcept { return localNameValue; }
const identity::SemanticImportBindingKey& ResolvedImportEdge::binding() const noexcept {
  return bindingValue;
}
const BindingTarget& ResolvedImportEdge::canonicalTarget() const noexcept {
  return canonicalTargetValue;
}
const identity::SourceSpan& ResolvedImportEdge::declarationSpan() const noexcept {
  return declarationSpanValue;
}
zc::Maybe<const identity::SourceSpan&> ResolvedImportEdge::aliasSpan() const noexcept {
  ZC_IF_SOME(value, aliasSpanValue) { return value; }
  return zc::none;
}
const identity::SourceSpan& ResolvedImportEdge::canonicalDeclarationSpan() const noexcept {
  return canonicalDeclarationSpanValue;
}
zc::Maybe<const identity::SourceSpan&> ResolvedImportEdge::exportSpan() const noexcept {
  ZC_IF_SOME(value, exportSpanValue) { return value; }
  return zc::none;
}
zc::ArrayPtr<const ReexportProvenanceStep> ResolvedImportEdge::sourceReexportChain()
    const noexcept {
  return sourceReexportChainValue.asPtr();
}

ResolvedModuleAlias::ResolvedModuleAlias(identity::ModuleId requester, ast::NodeId syntax,
                                         uint32_t schemaPreorderOrdinal, identity::DefId alias,
                                         BindingNameKey&& localName,
                                         identity::ModuleId targetModule,
                                         ExportSurfaceRevision targetRevision,
                                         identity::SourceSpan&& declarationSpan,
                                         identity::SourceSpan&& targetSpan, bool exported) noexcept
    : requesterValue(requester),
      syntaxValue(syntax),
      schemaPreorderOrdinalValue(schemaPreorderOrdinal),
      aliasValue(alias),
      localNameValue(zc::mv(localName)),
      targetModuleValue(targetModule),
      targetRevisionValue(targetRevision),
      declarationSpanValue(zc::mv(declarationSpan)),
      targetSpanValue(zc::mv(targetSpan)),
      exportedValue(exported) {}
identity::ModuleId ResolvedModuleAlias::requester() const noexcept { return requesterValue; }
ast::NodeId ResolvedModuleAlias::syntax() const noexcept { return syntaxValue; }
uint32_t ResolvedModuleAlias::schemaPreorderOrdinal() const noexcept {
  return schemaPreorderOrdinalValue;
}
identity::DefId ResolvedModuleAlias::alias() const noexcept { return aliasValue; }
const BindingNameKey& ResolvedModuleAlias::localName() const noexcept { return localNameValue; }
identity::ModuleId ResolvedModuleAlias::targetModule() const noexcept { return targetModuleValue; }
const ExportSurfaceRevision& ResolvedModuleAlias::targetRevision() const noexcept {
  return targetRevisionValue;
}
const identity::SourceSpan& ResolvedModuleAlias::declarationSpan() const noexcept {
  return declarationSpanValue;
}
const identity::SourceSpan& ResolvedModuleAlias::targetSpan() const noexcept {
  return targetSpanValue;
}
bool ResolvedModuleAlias::exported() const noexcept { return exportedValue; }

ResolvedLocalExportSpecifier::ResolvedLocalExportSpecifier(
    ast::NodeId syntax, uint32_t schemaPreorderOrdinal, identity::SemanticIdentifier&& sourceName,
    identity::SemanticIdentifier&& exportedName, identity::SourceSpan&& sourceNameSpan,
    identity::SourceSpan&& declarationSpan, zc::Maybe<identity::SourceSpan>&& aliasSpan,
    identity::SourceSpan&& exportSpan) noexcept
    : syntaxValue(syntax),
      schemaPreorderOrdinalValue(schemaPreorderOrdinal),
      sourceNameValue(zc::mv(sourceName)),
      exportedNameValue(zc::mv(exportedName)),
      sourceNameSpanValue(zc::mv(sourceNameSpan)),
      declarationSpanValue(zc::mv(declarationSpan)),
      aliasSpanValue(zc::mv(aliasSpan)),
      exportSpanValue(zc::mv(exportSpan)) {}
ast::NodeId ResolvedLocalExportSpecifier::syntax() const noexcept { return syntaxValue; }
uint32_t ResolvedLocalExportSpecifier::schemaPreorderOrdinal() const noexcept {
  return schemaPreorderOrdinalValue;
}
const identity::SemanticIdentifier& ResolvedLocalExportSpecifier::sourceName() const noexcept {
  return sourceNameValue;
}
const identity::SemanticIdentifier& ResolvedLocalExportSpecifier::exportedName() const noexcept {
  return exportedNameValue;
}
const identity::SourceSpan& ResolvedLocalExportSpecifier::sourceNameSpan() const noexcept {
  return sourceNameSpanValue;
}
const identity::SourceSpan& ResolvedLocalExportSpecifier::declarationSpan() const noexcept {
  return declarationSpanValue;
}
zc::Maybe<const identity::SourceSpan&> ResolvedLocalExportSpecifier::aliasSpan() const noexcept {
  ZC_IF_SOME(value, aliasSpanValue) { return value; }
  return zc::none;
}
const identity::SourceSpan& ResolvedLocalExportSpecifier::exportSpan() const noexcept {
  return exportSpanValue;
}

struct VerifiedBindingInput::Impl final {
  Impl(const BindingInputCandidate& candidate, identity::PackageKey&& packageKey,
       identity::CrateKey&& crateKey, identity::ModuleKey&& moduleKey,
       identity::SemanticContextFingerprint&& semanticFingerprint,
       zc::Vector<VerifiedExportSurfaceView>&& dependencySurfaces,
       zc::Maybe<VerifiedExportSurfaceView>&& preludeSurface,
       zc::Vector<ResolvedImportEdge>&& resolvedImports,
       zc::Vector<ResolvedModuleAlias>&& resolvedModuleAliases,
       zc::Vector<ResolvedLocalExportSpecifier>&& localExportSpecifiers)
      : semanticContext(candidate.semanticContext),
        package(candidate.package),
        packageKey(zc::mv(packageKey)),
        crate(candidate.crate),
        crateKey(zc::mv(crateKey)),
        module(candidate.module),
        moduleKey(zc::mv(moduleKey)),
        registries(candidate.registries),
        semanticFingerprint(zc::mv(semanticFingerprint)),
        parsedModule(candidate.parsedModule),
        definitions(candidate.definitions),
        dependencySurfaces(zc::mv(dependencySurfaces)),
        preludeSurface(zc::mv(preludeSurface)),
        resolvedImports(zc::mv(resolvedImports)),
        resolvedModuleAliases(zc::mv(resolvedModuleAliases)),
        localExportSpecifiers(zc::mv(localExportSpecifiers)) {}

  identity::SemanticContextBrand semanticContext;
  identity::PackageId package;
  identity::PackageKey packageKey;
  identity::CrateId crate;
  identity::CrateKey crateKey;
  identity::ModuleId module;
  identity::ModuleKey moduleKey;
  const identity::SemanticIdentityRegistrySet& registries;
  identity::SemanticContextFingerprint semanticFingerprint;
  const VerifiedParsedModule& parsedModule;
  const FrozenDefinitionInventoryView& definitions;
  zc::Vector<VerifiedExportSurfaceView> dependencySurfaces;
  zc::Maybe<VerifiedExportSurfaceView> preludeSurface;
  zc::Vector<ResolvedImportEdge> resolvedImports;
  zc::Vector<ResolvedModuleAlias> resolvedModuleAliases;
  zc::Vector<ResolvedLocalExportSpecifier> localExportSpecifiers;
};

VerifiedBindingInput::VerifiedBindingInput(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedBindingInput::~VerifiedBindingInput() noexcept(false) = default;
VerifiedBindingInput::VerifiedBindingInput(VerifiedBindingInput&&) noexcept = default;
VerifiedBindingInput& VerifiedBindingInput::operator=(VerifiedBindingInput&&) noexcept = default;
identity::SemanticContextBrand VerifiedBindingInput::semanticContext() const noexcept {
  return impl->semanticContext;
}
identity::PackageId VerifiedBindingInput::package() const noexcept { return impl->package; }
const identity::PackageKey& VerifiedBindingInput::packageKey() const noexcept {
  return impl->packageKey;
}
identity::CrateId VerifiedBindingInput::crate() const noexcept { return impl->crate; }
const identity::CrateKey& VerifiedBindingInput::crateKey() const noexcept { return impl->crateKey; }
identity::ModuleId VerifiedBindingInput::module() const noexcept { return impl->module; }
const identity::ModuleKey& VerifiedBindingInput::moduleKey() const noexcept {
  return impl->moduleKey;
}
zc::Maybe<const identity::ModuleKey&> VerifiedBindingInput::moduleKey(
    identity::ModuleId module) const noexcept {
  return impl->registries.modules().lookup(module);
}
zc::Maybe<const identity::DefinitionKey&> VerifiedBindingInput::definitionKey(
    identity::DefId definition) const noexcept {
  return impl->registries.definitions().lookup(definition);
}
const identity::SemanticContextFingerprint& VerifiedBindingInput::semanticFingerprint()
    const noexcept {
  return impl->semanticFingerprint;
}
const ast::Tree& VerifiedBindingInput::tree() const noexcept { return impl->parsedModule.tree(); }
const VerifiedParsedModule& VerifiedBindingInput::parsedModule() const noexcept {
  return impl->parsedModule;
}
const FrozenDefinitionInventoryView& VerifiedBindingInput::definitions() const noexcept {
  return impl->definitions;
}
zc::ArrayPtr<const VerifiedExportSurfaceView> VerifiedBindingInput::dependencySurfaces()
    const noexcept {
  return impl->dependencySurfaces.asPtr();
}
zc::Maybe<const VerifiedExportSurfaceView&> VerifiedBindingInput::preludeSurface() const noexcept {
  ZC_IF_SOME(value, impl->preludeSurface) { return value; }
  return zc::none;
}
zc::ArrayPtr<const ResolvedImportEdge> VerifiedBindingInput::resolvedImports() const noexcept {
  return impl->resolvedImports.asPtr();
}
zc::ArrayPtr<const ResolvedModuleAlias> VerifiedBindingInput::resolvedModuleAliases()
    const noexcept {
  return impl->resolvedModuleAliases.asPtr();
}
zc::ArrayPtr<const ResolvedLocalExportSpecifier> VerifiedBindingInput::localExportSpecifiers()
    const noexcept {
  return impl->localExportSpecifiers.asPtr();
}

BindingInputVerificationResult BindingInputVerifier::verify(
    const BindingInputCandidate& candidate) {
  const auto& registries = candidate.registries;
  const auto& tree = candidate.parsedModule.tree();
  zc::Maybe<identity::ModuleId> requester = candidate.module;
  const auto inputFailure = [&]() {
    return failure(ModuleGraphInvariantKind::InputMismatch, candidate.module);
  };
  const auto incomplete = [&]() {
    return failure(ModuleGraphInvariantKind::IncompleteResolution, candidate.module);
  };
  if (!candidate.semanticContext.isValid() || !allRegistriesFrozen(registries) ||
      !candidate.package.belongsTo(candidate.semanticContext) ||
      !candidate.crate.belongsTo(candidate.semanticContext) ||
      !candidate.module.belongsTo(candidate.semanticContext) ||
      !candidate.parsedModule.sourceFile().belongsTo(candidate.semanticContext) ||
      candidate.moduleGraph.semanticContext() != candidate.semanticContext ||
      candidate.moduleGraph.requester() != candidate.module ||
      candidate.definitions.semanticContext() != candidate.semanticContext ||
      candidate.definitions.module() != candidate.module) {
    return inputFailure();
  }
  auto package = registries.packages().lookup(candidate.package);
  auto crate = registries.crates().lookup(candidate.crate);
  auto source = registries.sourceFiles().lookup(candidate.parsedModule.sourceFile());
  auto module = registries.modules().lookup(candidate.module);
  if (package == zc::none || crate == zc::none || source == zc::none || module == zc::none ||
      registries.sourceSnapshot(candidate.parsedModule.sourceFile()) == zc::none) {
    return inputFailure();
  }
  zc::Maybe<identity::PackageKey> verifiedPackage;
  zc::Maybe<identity::CrateKey> verifiedCrate;
  zc::Maybe<identity::ModuleKey> verifiedModule;
  zc::Maybe<identity::SemanticContextFingerprint> verifiedFingerprint;
  ZC_IF_SOME(packageValue, package) {
    ZC_IF_SOME(crateValue, crate) {
      ZC_IF_SOME(sourceValue, source) {
        ZC_IF_SOME(moduleValue, module) {
          if (packageValue.encode() != crateValue.package().encode() ||
              !sourceValue.belongsTo(crateValue) ||
              moduleValue.crate().encode() != crateValue.encode() ||
              !candidate.parsedModule.source().sameAs(sourceValue)) {
            return inputFailure();
          }
          const auto& fingerprintValue = candidate.moduleGraph.semanticFingerprint();
          auto revision = computeModuleGraphRevision(
              fingerprintValue, candidate.moduleGraph.modules(), candidate.moduleGraph.edges());
          if (revision == zc::none) { return inputFailure(); }
          ZC_IF_SOME(revisionValue, revision) {
            if (revisionValue.digest() != candidate.moduleGraph.revision().digest()) {
              return failure(ModuleGraphInvariantKind::RevisionMismatch, zc::mv(requester));
            }
          }
          if (!tree.contains(tree.root()) ||
              tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
            return inputFailure();
          }
          for (const auto& definition : candidate.definitions.definitions()) {
            if (!definition.definition.belongsTo(candidate.semanticContext)) {
              return inputFailure();
            }
            auto record = registries.definitions().lookupRecord(definition.definition);
            if (record == zc::none) { return inputFailure(); }
            ZC_IF_SOME(recordValue, record) {
              if (recordValue.module().encode() != moduleValue.encode()) { return inputFailure(); }
            }
          }
          verifiedPackage = packageValue.clone();
          verifiedCrate = crateValue.clone();
          verifiedModule = moduleValue.clone();
          verifiedFingerprint = fingerprintValue.clone();
        }
      }
    }
  }
  auto schemaOrdinalResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalResult == zc::none) { return inputFailure(); }
  zc::Vector<uint32_t> schemaOrdinals;
  ZC_IF_SOME(values, schemaOrdinalResult) { schemaOrdinals = zc::mv(values); }

  zc::TreeMap<zc::String, identity::ModuleId> expectedDependencyTargets;
  zc::Maybe<identity::ModuleId> expectedPreludeTarget;
  for (const auto& edge : candidate.moduleGraph.edges()) {
    if (edge.request().requester() != candidate.module) { continue; }
    if (!edge.target().belongsTo(candidate.semanticContext)) { return inputFailure(); }
    auto target = registries.modules().lookup(edge.target());
    if (target == zc::none) { return inputFailure(); }
    if (edge.request().kind() == identity::ModuleDependencyKind::Prelude) {
      ZC_IF_SOME(previous, expectedPreludeTarget) {
        if (previous != edge.target()) {
          return failure(ModuleGraphInvariantKind::InvalidPrelude, candidate.module);
        }
      }
      expectedPreludeTarget = edge.target();
      continue;
    }
    ZC_IF_SOME(targetValue, target) {
      auto key = zc::encodeHex(targetValue.encode().asPtr());
      if (expectedDependencyTargets.find(key) == zc::none) {
        expectedDependencyTargets.insert(zc::mv(key), edge.target());
      }
    }
  }

  if (candidate.dependencySurfaces.size() != expectedDependencyTargets.size()) {
    return incomplete();
  }
  zc::TreeMap<zc::String, size_t> providedSurfaces;
  zc::Array<uint8_t> previousSurface;
  bool hasPreviousSurface = false;
  for (size_t index = 0; index < candidate.dependencySurfaces.size(); ++index) {
    const auto& dependency = candidate.dependencySurfaces[index];
    auto moduleKey = registries.modules().lookup(dependency.sourceModule);
    if (moduleKey == zc::none || dependency.sourceModule != dependency.surface.sourceModule()) {
      return inputFailure();
    }
    ZC_IF_SOME(keyValue, moduleKey) {
      auto encoded = keyValue.encode();
      if ((hasPreviousSurface && !(previousSurface.asPtr() < encoded.asPtr())) ||
          expectedDependencyTargets.find(zc::encodeHex(encoded.asPtr())) == zc::none) {
        return incomplete();
      }
      auto mapKey = zc::encodeHex(encoded.asPtr());
      if (providedSurfaces.find(mapKey) != zc::none) { return incomplete(); }
      providedSurfaces.insert(zc::mv(mapKey), index);
      previousSurface = zc::mv(encoded);
      hasPreviousSurface = true;
    }
    switch (validateSurface(candidate, dependency.sourceModule, dependency.surface)) {
      case SurfaceValidation::Valid:
        break;
      case SurfaceValidation::InputMismatch:
        return inputFailure();
      case SurfaceValidation::InvalidEdge:
        return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
      case SurfaceValidation::RevisionMismatch:
        return failure(ModuleGraphInvariantKind::RevisionMismatch, candidate.module);
    }
  }
  for (const auto& target : expectedDependencyTargets) {
    if (providedSurfaces.find(target.key) == zc::none) { return incomplete(); }
  }

  if ((expectedPreludeTarget == zc::none) != (candidate.preludeSurface == zc::none)) {
    return incomplete();
  }
  ZC_IF_SOME(target, expectedPreludeTarget) {
    ZC_IF_SOME(surface, candidate.preludeSurface) {
      if (surface.sourceModule() != target) {
        return failure(ModuleGraphInvariantKind::InvalidPrelude, candidate.module);
      }
      switch (validateSurface(candidate, target, surface)) {
        case SurfaceValidation::Valid:
          break;
        case SurfaceValidation::InputMismatch:
          return inputFailure();
        case SurfaceValidation::InvalidEdge:
          return failure(ModuleGraphInvariantKind::InvalidPrelude, candidate.module);
        case SurfaceValidation::RevisionMismatch:
          return failure(ModuleGraphInvariantKind::RevisionMismatch, candidate.module);
      }
    }
  }

  zc::Vector<VerifiedExportSurfaceView> dependencyViews(expectedDependencyTargets.size());
  for (const auto& target : expectedDependencyTargets) {
    ZC_IF_SOME(index, providedSurfaces.find(target.key)) {
      const auto& surface = candidate.dependencySurfaces[index].surface;
      const auto entries =
          candidate.module == surface.sourceModule() ? surface.visibleEntries() : surface.exports();
      zc::Vector<ExportSurfaceEntry> visibleEntries(entries.size());
      for (const auto& entry : entries) { visibleEntries.add(entry.clone()); }
      dependencyViews.add(VerifiedExportSurfaceView(zc::heap<VerifiedExportSurfaceView::Impl>(
          candidate.module, surface.sourceModule(), surface.revision(), zc::mv(visibleEntries))));
    }
  }
  zc::Maybe<VerifiedExportSurfaceView> preludeView;
  ZC_IF_SOME(surface, candidate.preludeSurface) {
    const auto entries =
        candidate.module == surface.sourceModule() ? surface.visibleEntries() : surface.exports();
    zc::Vector<ExportSurfaceEntry> visibleEntries(entries.size());
    for (const auto& entry : entries) { visibleEntries.add(entry.clone()); }
    preludeView = VerifiedExportSurfaceView(zc::heap<VerifiedExportSurfaceView::Impl>(
        candidate.module, surface.sourceModule(), surface.revision(), zc::mv(visibleEntries)));
  }

  zc::TreeMap<zc::String, ResolvedImportEdge> importFacts;
  zc::TreeMap<zc::String, ResolvedModuleAlias> moduleAliasFacts;
  zc::TreeMap<uint64_t, ResolvedLocalExportSpecifier> localExportFacts;
  zc::TreeMap<zc::String, BindingInputSourceFailure> sourceFailures;
  for (const auto& edge : candidate.moduleGraph.edges()) {
    const auto& request = edge.request();
    if (request.requester() != candidate.module ||
        request.kind() == identity::ModuleDependencyKind::Prelude) {
      continue;
    }
    auto targetModule = registries.modules().lookup(edge.target());
    if (targetModule == zc::none) { return inputFailure(); }
    zc::Maybe<const VerifiedExportSurface&> selectedSurface;
    ZC_IF_SOME(targetValue, targetModule) {
      auto key = zc::encodeHex(targetValue.encode().asPtr());
      ZC_IF_SOME(index, providedSurfaces.find(key)) {
        selectedSurface = candidate.dependencySurfaces[index].surface;
      }
    }
    if (selectedSurface == zc::none || request.isPrelude()) {
      return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
    }
    for (const auto& requestSite : request.syntaxSites()) {
      const auto syntax = requestSite.node;
      if (!tree.contains(syntax)) {
        return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
      }
      auto syntaxSpan = candidate.parsedModule.spanFor(tree.node(syntax).range);
      if (syntaxSpan == zc::none) {
        return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
      }
      ZC_IF_SOME(span, syntaxSpan) {
        if (!sameSourceSpan(span, requestSite.span)) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
        }
      }

      ZC_IF_SOME(surface, selectedSurface) {
        if (request.kind() == identity::ModuleDependencyKind::ModuleAlias) {
          const auto& syntaxNode = tree.node(syntax);
          if (syntaxNode.kind != ast::SyntaxKind::ModuleDeclaration ||
              static_cast<ast::ModuleDeclarationForm>(
                  syntaxNode.payload.words[ast::kModuleDeclarationFormWord]) !=
                  ast::ModuleDeclarationForm::Alias) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          auto definition = definitionAt(candidate.definitions, syntax);
          if (definition == zc::none) { return incomplete(); }
          const ast::NodeId targetPath(
              syntaxNode.payload.words[ast::kModuleDeclarationAliasTargetWord]);
          auto targetSpan = tree.contains(targetPath)
                                ? candidate.parsedModule.spanFor(tree.node(targetPath).range)
                                : zc::Maybe<identity::SourceSpan>();
          if (targetSpan == zc::none) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          ZC_IF_SOME(definitionValue, definition) {
            if (definitionValue.record.kind() != identity::DefinitionKind::ModuleAlias ||
                definitionValue.bindingName == zc::none || syntax.value >= schemaOrdinals.size()) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
            }
            auto key = zc::encodeHex(definitionValue.key.encode().asPtr());
            if (moduleAliasFacts.find(key) != zc::none) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
            }
            ZC_IF_SOME(targetSpanValue, targetSpan) {
              ZC_IF_SOME(bindingName, definitionValue.bindingName) {
                moduleAliasFacts.insert(
                    zc::mv(key),
                    ResolvedModuleAlias(
                        candidate.module, syntax, schemaOrdinals[syntax.value],
                        definitionValue.definition,
                        BindingNameKey(Namespace::Module, bindingName.clone()), edge.target(),
                        surface.revision(), definitionValue.source.clone(), targetSpanValue.clone(),
                        syntaxNode.payload.words[ast::kModuleDeclarationExportedAliasWord] != 0));
              }
            }
          }
          continue;
        }

        const auto expectedSyntax =
            request.kind() == identity::ModuleDependencyKind::ForeignReexport
                ? ast::SyntaxKind::ExportDeclaration
                : ast::SyntaxKind::ImportDeclaration;
        if (tree.node(syntax).kind != expectedSyntax) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
        }
        const auto& syntaxNode = tree.node(syntax);
        const ast::NodeList specifiers{
            syntaxNode.payload.words[expectedSyntax == ast::SyntaxKind::ImportDeclaration
                                         ? ast::kImportDeclarationSpecifiersFirstWord
                                         : ast::kExportDeclarationSpecifiersFirstWord],
            syntaxNode.payload.words[expectedSyntax == ast::SyntaxKind::ImportDeclaration
                                         ? ast::kImportDeclarationSpecifiersSizeWord
                                         : ast::kExportDeclarationSpecifiersSizeWord]};
        if (!tree.contains(specifiers)) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
        }
        const auto importKind = request.kind() == identity::ModuleDependencyKind::ForeignReexport
                                    ? ImportBindingKind::ForeignReexport
                                    : ImportBindingKind::Import;
        if (specifiers.empty()) {
          if (syntax.value >= schemaOrdinals.size() || request.normalizedPath().size() == 0) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          const auto& sourceSegment = request.normalizedPath().back();
          auto sourceName = identity::DeclaredDefinitionName::fromCanonical(sourceSegment.text());
          ast::IdentId localIdentifier;
          if (request.kind() == identity::ModuleDependencyKind::Import) {
            localIdentifier =
                ast::IdentId(syntaxNode.payload.words[ast::kImportDeclarationAliasWord]);
          }
          auto localName = identity::DeclaredDefinitionName::fromCanonical(
              localIdentifier ? tree.ident(localIdentifier) : sourceSegment.text());
          if (sourceName == zc::none || localName == zc::none || verifiedModule == zc::none) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          zc::Maybe<identity::SourceSpan> aliasSpan;
          if (localIdentifier) {
            const ast::NodeId path(syntaxNode.payload.words[ast::kImportDeclarationPathWord]);
            if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
            }
            const auto& pathNode = tree.node(path);
            const ast::IdentList segments{pathNode.payload.words[ast::kModulePathSegmentsFirstWord],
                                          pathNode.payload.words[ast::kModulePathSegmentsSizeWord]};
            if (segments.size > (UINT32_MAX - 1) / 2) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
            }
            aliasSpan = explicitAliasSpan(candidate.parsedModule, syntax, expectedSyntax,
                                          segments.size * 2 + 1);
            if (aliasSpan == zc::none) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
            }
          }
          zc::Maybe<identity::SemanticImportBindingKey> semanticBinding;
          ZC_IF_SOME(requesterKey, verifiedModule) {
            ZC_IF_SOME(sourceNameValue, sourceName) {
              ZC_IF_SOME(localNameValue, localName) {
                semanticBinding = identity::SemanticImportBindingKey::from(
                    requesterKey.clone(), request.key().clone(),
                    semanticImportOperation(importKind), identity::DefinitionNamespace::Module,
                    sourceNameValue.clone(), identity::DefinitionNamespace::Module,
                    localNameValue.clone());
              }
            }
          }
          if (semanticBinding == zc::none) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          zc::Maybe<BindingNameKey> noRequestedName;
          zc::Maybe<identity::SourceSpan> exportSpan;
          ZC_IF_SOME(declarationSpan, syntaxSpan) {
            if (importKind == ImportBindingKind::ForeignReexport) {
              exportSpan = declarationSpan.clone();
            }
            zc::Vector<ReexportProvenanceStep> noSourceChain;
            ZC_IF_SOME(bindingValue, semanticBinding) {
              ZC_IF_SOME(localNameValue, localName) {
                auto key =
                    resolvedImportOrderKey(bindingValue, schemaOrdinals[syntax.value], syntax);
                if (importFacts.find(key) != zc::none) {
                  return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
                }
                importFacts.insert(
                    zc::mv(key),
                    ResolvedImportEdge(
                        candidate.module, syntax, schemaOrdinals[syntax.value], importKind,
                        edge.target(), surface.revision(), zc::mv(noRequestedName),
                        BindingNameKey(Namespace::Module, localNameValue.clone()),
                        zc::mv(bindingValue), BindingTarget::module(edge.target()),
                        declarationSpan.clone(), zc::mv(aliasSpan), declarationSpan.clone(),
                        zc::mv(exportSpan), zc::mv(noSourceChain)));
              }
            }
          }
          continue;
        }

        for (const auto specifier : tree.list(specifiers)) {
          const auto specifierKind =
              request.kind() == identity::ModuleDependencyKind::ForeignReexport
                  ? ast::SyntaxKind::ExportSpecifier
                  : ast::SyntaxKind::ImportSpecifier;
          if (!tree.contains(specifier) || tree.node(specifier).kind != specifierKind) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          const auto& specifierNode = tree.node(specifier);
          const ast::IdentId sourceName(
              specifierNode.payload.words[specifierKind == ast::SyntaxKind::ImportSpecifier
                                              ? ast::kImportSpecifierNameWord
                                              : ast::kExportSpecifierNameWord]);
          const ast::IdentId aliasName(
              specifierNode.payload.words[specifierKind == ast::SyntaxKind::ImportSpecifier
                                              ? ast::kImportSpecifierAliasWord
                                              : ast::kExportSpecifierAliasWord]);
          if (!sourceName) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          auto requestedIdentifier =
              identity::SemanticIdentifier::fromSource(tree.ident(sourceName));
          auto requestedModulePath = renderModuleRequestPath(request);
          if (requestedIdentifier == zc::none || requestedModulePath == zc::none) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          zc::Vector<size_t> selectedEntries;
          bool matchedSurfaceName = false;
          const auto visibleEntries = surface.visibleEntries();
          for (size_t index = 0; index < visibleEntries.size(); ++index) {
            if (visibleEntries[index].name.name().text() != tree.ident(sourceName)) { continue; }
            matchedSurfaceName = true;
            bool visible = candidate.module == surface.sourceModule();
            if (!visible) {
              for (const auto& exported : surface.exports()) {
                if (visibleEntries[index].name.nameSpace() == exported.name.nameSpace() &&
                    visibleEntries[index].name.name() == exported.name.name()) {
                  visible = true;
                  break;
                }
              }
            }
            if (visible) { selectedEntries.add(index); }
          }
          auto source = candidate.parsedModule.spanFor(specifierNode.range);
          if (source == zc::none) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          if (selectedEntries.empty()) {
            ZC_IF_SOME(sourceValue, source) {
              const auto diagnostic =
                  matchedSurfaceName
                      ? (request.kind() == identity::ModuleDependencyKind::ForeignReexport
                             ? BindingInputDiagnostic::ReexportTargetNotVisible
                             : BindingInputDiagnostic::ImportTargetNotVisible)
                      : (request.kind() == identity::ModuleDependencyKind::ForeignReexport
                             ? BindingInputDiagnostic::ReexportMemberNotFound
                             : BindingInputDiagnostic::ImportMemberNotFound);
              ZC_IF_SOME(pathValue, requestedModulePath) {
                ZC_IF_SOME(identifierValue, requestedIdentifier) {
                  BindingInputSourceFailure sourceFailure(diagnostic, specifier,
                                                          sourceValue.clone(), zc::str(pathValue),
                                                          identifierValue.clone());
                  auto key = bindingInputFailureKey(sourceFailure);
                  if (sourceFailures.find(key) != zc::none) {
                    return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
                  }
                  sourceFailures.insert(zc::mv(key), zc::mv(sourceFailure));
                }
              }
            }
            continue;
          }
          if (specifier.value >= schemaOrdinals.size() || verifiedModule == zc::none) {
            return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
          }
          for (const auto selectedIndex : selectedEntries) {
            const auto& entry = visibleEntries[selectedIndex];
            auto sourceNamespace = semanticNamespace(entry.name.nameSpace());
            auto sourceDefinitionName =
                identity::DeclaredDefinitionName::fromCanonical(tree.ident(sourceName));
            auto localDefinitionName = identity::DeclaredDefinitionName::fromCanonical(
                aliasName ? tree.ident(aliasName) : tree.ident(sourceName));
            if (sourceNamespace == zc::none || sourceDefinitionName == zc::none ||
                localDefinitionName == zc::none) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
            }
            zc::Maybe<identity::SourceSpan> aliasSpan;
            if (aliasName) {
              aliasSpan = explicitAliasSpan(candidate.parsedModule, specifier, specifierKind, 2);
              if (aliasSpan == zc::none) {
                return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
              }
            }
            zc::Maybe<identity::SemanticImportBindingKey> semanticBinding;
            ZC_IF_SOME(requesterKey, verifiedModule) {
              ZC_IF_SOME(namespaceValue, sourceNamespace) {
                ZC_IF_SOME(sourceNameValue, sourceDefinitionName) {
                  ZC_IF_SOME(localNameValue, localDefinitionName) {
                    semanticBinding = identity::SemanticImportBindingKey::from(
                        requesterKey.clone(), request.key().clone(),
                        semanticImportOperation(importKind), namespaceValue,
                        sourceNameValue.clone(), namespaceValue, localNameValue.clone());
                  }
                }
              }
            }
            if (semanticBinding == zc::none) {
              return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
            }
            zc::Maybe<BindingNameKey> requestedName(entry.name.clone());
            zc::Maybe<identity::SourceSpan> exportSpan;
            ZC_IF_SOME(sourceValue, source) {
              if (importKind == ImportBindingKind::ForeignReexport) {
                exportSpan = sourceValue.clone();
              }
              ZC_IF_SOME(bindingValue, semanticBinding) {
                ZC_IF_SOME(localNameValue, localDefinitionName) {
                  auto key = resolvedImportOrderKey(bindingValue, schemaOrdinals[specifier.value],
                                                    specifier);
                  if (importFacts.find(key) != zc::none) {
                    return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
                  }
                  importFacts.insert(
                      zc::mv(key),
                      ResolvedImportEdge(
                          candidate.module, specifier, schemaOrdinals[specifier.value], importKind,
                          edge.target(), surface.revision(), zc::mv(requestedName),
                          BindingNameKey(entry.name.nameSpace(), localNameValue.clone()),
                          zc::mv(bindingValue), entry.canonicalTarget.clone(), sourceValue.clone(),
                          zc::mv(aliasSpan), entry.canonicalDeclarationSpan.clone(),
                          zc::mv(exportSpan), cloneReexportChain(entry.reexportChain.asPtr())));
                }
              }
            }
          }
        }
      }
    }
  }

  bool validLocalExports = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (!validLocalExports || syntax.kind != ast::SyntaxKind::ExportDeclaration) { return; }
    const ast::NodeId declaration(syntax.payload.words[ast::kExportDeclarationDeclarationWord]);
    const ast::NodeId path(syntax.payload.words[ast::kExportDeclarationPathWord]);
    const ast::NodeList specifiers{syntax.payload.words[ast::kExportDeclarationSpecifiersFirstWord],
                                   syntax.payload.words[ast::kExportDeclarationSpecifiersSizeWord]};
    if (tree.contains(declaration) || tree.contains(path) || specifiers.empty()) { return; }
    if (!tree.contains(specifiers)) {
      validLocalExports = false;
      return;
    }
    auto exportSpan = candidate.parsedModule.spanFor(syntax.range);
    if (exportSpan == zc::none) {
      validLocalExports = false;
      return;
    }
    for (const auto specifier : tree.list(specifiers)) {
      if (!tree.contains(specifier) ||
          tree.node(specifier).kind != ast::SyntaxKind::ExportSpecifier ||
          specifier.value >= schemaOrdinals.size()) {
        validLocalExports = false;
        return;
      }
      const auto& specifierSyntax = tree.node(specifier);
      const ast::IdentId sourceIdentifier(
          specifierSyntax.payload.words[ast::kExportSpecifierNameWord]);
      const ast::IdentId aliasIdentifier(
          specifierSyntax.payload.words[ast::kExportSpecifierAliasWord]);
      if (!sourceIdentifier) {
        validLocalExports = false;
        return;
      }
      auto sourceName = identity::SemanticIdentifier::fromSource(tree.ident(sourceIdentifier));
      auto exportedName =
          aliasIdentifier ? identity::SemanticIdentifier::fromSource(tree.ident(aliasIdentifier))
                          : identity::SemanticIdentifier::fromSource(tree.ident(sourceIdentifier));
      auto sourceNameSpan =
          candidate.parsedModule.retainedTokenSpan(specifier, 0, ast::SyntaxKind::Identifier);
      if (sourceName == zc::none || exportedName == zc::none || sourceNameSpan == zc::none) {
        validLocalExports = false;
        return;
      }
      zc::Maybe<identity::SourceSpan> aliasSpan;
      if (aliasIdentifier) {
        aliasSpan = explicitAliasSpan(candidate.parsedModule, specifier,
                                      ast::SyntaxKind::ExportSpecifier, 2);
        if (aliasSpan == zc::none) {
          validLocalExports = false;
          return;
        }
      }
      const uint64_t key = (uint64_t(schemaOrdinals[specifier.value]) << 32) | specifier.value;
      if (localExportFacts.find(key) != zc::none) {
        validLocalExports = false;
        return;
      }
      auto declarationSpan = candidate.parsedModule.spanFor(specifierSyntax.range);
      if (declarationSpan == zc::none) {
        validLocalExports = false;
        return;
      }
      ZC_IF_SOME(sourceNameValue, sourceName) {
        ZC_IF_SOME(exportedNameValue, exportedName) {
          ZC_IF_SOME(sourceNameSpanValue, sourceNameSpan) {
            ZC_IF_SOME(declarationSpanValue, declarationSpan) {
              ZC_IF_SOME(exportSpanValue, exportSpan) {
                localExportFacts.insert(
                    key,
                    ResolvedLocalExportSpecifier(
                        specifier, schemaOrdinals[specifier.value], zc::mv(sourceNameValue),
                        zc::mv(exportedNameValue), sourceNameSpanValue.clone(),
                        declarationSpanValue.clone(), zc::mv(aliasSpan), exportSpanValue.clone()));
              }
            }
          }
        }
      }
    }
  });
  if (!validLocalExports) {
    return failure(ModuleGraphInvariantKind::InvalidEdge, candidate.module);
  }

  if (sourceFailures.size() != 0) {
    zc::Vector<BindingInputSourceFailure> failures(sourceFailures.size());
    for (auto& entry : sourceFailures) { failures.add(zc::mv(entry.value)); }
    return BindingInputSourceRejected(zc::mv(failures));
  }
  zc::Vector<ResolvedImportEdge> resolvedImports(importFacts.size());
  for (auto& entry : importFacts) { resolvedImports.add(zc::mv(entry.value)); }
  zc::Vector<ResolvedModuleAlias> resolvedModuleAliases(moduleAliasFacts.size());
  for (auto& entry : moduleAliasFacts) { resolvedModuleAliases.add(zc::mv(entry.value)); }
  zc::Vector<ResolvedLocalExportSpecifier> localExportSpecifiers(localExportFacts.size());
  for (auto& entry : localExportFacts) { localExportSpecifiers.add(zc::mv(entry.value)); }

  ZC_IF_SOME(packageValue, verifiedPackage) {
    ZC_IF_SOME(crateValue, verifiedCrate) {
      ZC_IF_SOME(moduleValue, verifiedModule) {
        ZC_IF_SOME(fingerprintValue, verifiedFingerprint) {
          return VerifiedBindingInput(zc::heap<VerifiedBindingInput::Impl>(
              candidate, zc::mv(packageValue), zc::mv(crateValue), zc::mv(moduleValue),
              zc::mv(fingerprintValue), zc::mv(dependencyViews), zc::mv(preludeView),
              zc::mv(resolvedImports), zc::mv(resolvedModuleAliases),
              zc::mv(localExportSpecifiers)));
        }
      }
    }
  }
  return inputFailure();
}

}  // namespace zomlang::compiler::binder
