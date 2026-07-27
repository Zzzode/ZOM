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
#include "zomlang/compiler/binder/internal/verified-module-graph-storage.h"
#include "zomlang/compiler/binder/module-dependency-requests.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/core-library-query-provider.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/parser/parse-source-query.h"

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
  return registries.compilationUnits().isFrozen() && registries.crates().isFrozen() &&
         registries.sourceFiles().isFrozen() && registries.modules().isFrozen() &&
         registries.definitions().isFrozen() && registries.impls().isFrozen();
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
  if (value.is<GenericParameterBindingTarget>()) {
    const auto parameter = value.get<GenericParameterBindingTarget>().parameter;
    if (!parameter.belongsTo(context)) { return false; }
    auto key = registries.genericParameters().lookup(parameter);
    if (key == zc::none) { return false; }
    encoder.encodeUint8(0x02);
    ZC_IF_SOME(keyValue, key) {
      keyValue.encode(encoder);
      return true;
    }
    ZC_UNREACHABLE;
  }
  if (value.is<CallableParameterBindingTarget>()) {
    const auto parameter = value.get<CallableParameterBindingTarget>().parameter;
    if (!parameter.belongsTo(context)) { return false; }
    auto key = registries.callableParameters().lookup(parameter);
    if (key == zc::none) { return false; }
    encoder.encodeUint8(0x03);
    ZC_IF_SOME(keyValue, key) {
      keyValue.encode(encoder);
      return true;
    }
    ZC_UNREACHABLE;
  }
  if (value.is<OwnerLocalBindingTarget>()) { return false; }
  if (value.is<SemanticImportBindingTarget>()) {
    encoder.encodeUint8(0x05);
    encoder.encodeByteString(value.get<SemanticImportBindingTarget>().binding.encode().asPtr());
    return true;
  }
  const auto module = value.get<ModuleBindingTarget>().module;
  if (!module.belongsTo(context)) { return false; }
  auto key = registries.modules().lookup(module);
  if (key == zc::none) { return false; }
  encoder.encodeUint8(0x06);
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
    ZC_IF_SOME(moduleValue, module) {
      moduleValue.encode(stepEncoder);
      if (!encodeSurfaceTarget(stepEncoder, registries, context, step.bindingIdentity) ||
          !encodeSurfaceTarget(stepEncoder, registries, context, step.canonicalTarget)) {
        return false;
      }
      step.exportSpan.encode(stepEncoder);
      auto stepBytes = stepEncoder.finish();
      auto stepKey = zc::encodeHex(stepBytes.asPtr());
      if (seenSteps.find(stepKey) != zc::none) { return false; }
      seenSteps.insert(zc::mv(stepKey), true);
      moduleValue.encode(encoder);
      if (!encodeSurfaceTarget(encoder, registries, context, step.bindingIdentity) ||
          !encodeSurfaceTarget(encoder, registries, context, step.canonicalTarget)) {
        return false;
      }
      step.exportSpan.encode(encoder);
    }
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
      !surface.sourceCompilationUnit().belongsTo(candidate.semanticContext)) {
    return SurfaceValidation::InputMismatch;
  }
  auto module = registries.modules().lookup(surface.sourceModule());
  auto compilationUnit = registries.compilationUnits().lookup(surface.sourceCompilationUnit());
  if (module == zc::none || compilationUnit == zc::none) {
    return SurfaceValidation::InputMismatch;
  }
  ZC_IF_SOME(moduleValue, module) {
    ZC_IF_SOME(compilationUnitValue, compilationUnit) {
      if (moduleValue.crate().unit().encode().asPtr() != compilationUnitValue.encode().asPtr()) {
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
          const auto compilationUnitBytes = compilationUnitValue.encode();
          auto revision = ExportSurfaceRevision::computeFramed(
              candidate.moduleGraph.semanticFingerprint().digest(), moduleBytes.asPtr(),
              compilationUnitBytes.asPtr(), visibleValue.asPtr(), exportsValue.asPtr());
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

bool findToolchainRootPathForBuilder(const ast::Tree& tree, ast::NodeId current, ast::NodeId target,
                                     zc::Vector<uint32_t>& path) {
  if (current == target) { return true; }
  uint32_t childIndex = 0;
  bool found = false;
  ast::visitChildNodeIds(tree, tree.node(current), [&](ast::NodeId child) {
    const uint32_t currentIndex = childIndex++;
    if (found || !tree.contains(child)) { return; }
    path.add(currentIndex);
    if (findToolchainRootPathForBuilder(tree, child, target, path)) {
      found = true;
      return;
    }
    path.removeLast();
  });
  return found;
}

}  // namespace

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

struct ModuleGraphSourceFailure::Impl final {
  Impl(identity::ModuleKey&& module, identity::SourceFileKey&& source,
       LocalSyntaxPath&& declaredNamePath, uint32_t schemaPreorderOrdinal,
       diagnostics::ToolchainModuleRootArgument&& argument) noexcept
      : module(zc::mv(module)),
        source(zc::mv(source)),
        declaredNamePath(zc::mv(declaredNamePath)),
        schemaPreorderOrdinal(schemaPreorderOrdinal),
        argument(zc::mv(argument)) {}

  identity::ModuleKey module;
  identity::SourceFileKey source;
  LocalSyntaxPath declaredNamePath;
  uint32_t schemaPreorderOrdinal;
  diagnostics::ToolchainModuleRootArgument argument;
};

ModuleGraphSourceFailure::~ModuleGraphSourceFailure() noexcept(false) = default;
ModuleGraphSourceFailure::ModuleGraphSourceFailure(ModuleGraphSourceFailure&&) noexcept = default;
ModuleGraphSourceFailure& ModuleGraphSourceFailure::operator=(ModuleGraphSourceFailure&&) noexcept =
    default;

ModuleGraphSourceFailure::ModuleGraphSourceFailure(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    LocalSyntaxPath&& declaredNamePath, uint32_t schemaPreorderOrdinal,
    diagnostics::ToolchainModuleRootArgument&& argument) noexcept
    : impl(zc::heap<Impl>(zc::mv(module), zc::mv(source), zc::mv(declaredNamePath),
                          schemaPreorderOrdinal, zc::mv(argument))) {}
const identity::ModuleKey& ModuleGraphSourceFailure::module() const noexcept {
  return impl->module;
}
const identity::SourceFileKey& ModuleGraphSourceFailure::source() const noexcept {
  return impl->source;
}
const LocalSyntaxPath& ModuleGraphSourceFailure::declaredNamePath() const noexcept {
  return impl->declaredNamePath;
}
uint32_t ModuleGraphSourceFailure::schemaPreorderOrdinal() const noexcept {
  return impl->schemaPreorderOrdinal;
}
const diagnostics::ToolchainModuleRootArgument& ModuleGraphSourceFailure::argument()
    const noexcept {
  return impl->argument;
}

zc::Maybe<ModuleGraphSourceFailure>
ModuleGraphSourceFailureBuilder::buildToolchainModuleRootReserved(
    const ModuleGraphModule& module, const ParsedModuleGraphInput& parsed) {
  if (module.module() != parsed.module || module.key().path().size() != 1 ||
      module.key().crate().unit().kind() != identity::CompilationUnitKind::UserPackage ||
      !parsed.parsedModule.source().belongsTo(module.key().crate())) {
    return zc::none;
  }
  const auto& tree = parsed.parsedModule.tree();
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return zc::none;
  }
  const auto& source = tree.node(tree.root());
  const ast::NodeId declaration(source.payload.words[ast::kSourceFileModuleWord]);
  if (!tree.contains(declaration) ||
      tree.node(declaration).kind != ast::SyntaxKind::ModuleDeclaration) {
    return zc::none;
  }
  const auto& declarationNode = tree.node(declaration);
  const auto form = static_cast<ast::ModuleDeclarationForm>(
      declarationNode.payload.words[ast::kModuleDeclarationFormWord]);
  if (form == ast::ModuleDeclarationForm::Alias) { return zc::none; }

  auto segment = identity::ModulePathSegment::fromSource(tree.ident(
      ast::IdentId(declarationNode.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
  if (segment == zc::none) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> argumentPath;
  ZC_IF_SOME(value, segment) { argumentPath.add(zc::mv(value)); }
  auto argument = diagnostics::ToolchainModuleRootArgument::fromCanonicalPath(zc::mv(argumentPath));
  if (argument == zc::none) { return zc::none; }

  zc::Vector<uint32_t> pathComponents;
  if (!findToolchainRootPathForBuilder(tree, tree.root(), declaration, pathComponents)) {
    return zc::none;
  }
  auto declaredNamePath = LocalSyntaxPath::from(zc::mv(pathComponents));
  if (declaredNamePath == zc::none) { return zc::none; }

  uint32_t schemaPreorderOrdinal = 0;
  bool foundOrdinal = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (foundOrdinal) { return; }
    if (node == declaration) {
      foundOrdinal = true;
      return;
    }
    ++schemaPreorderOrdinal;
  });
  if (!foundOrdinal) { return zc::none; }
  ZC_IF_SOME(path, declaredNamePath) {
    ZC_IF_SOME(argumentValue, argument) {
      return ModuleGraphSourceFailure(module.key().clone(), parsed.parsedModule.source().clone(),
                                      zc::mv(path), schemaPreorderOrdinal, zc::mv(argumentValue));
    }
  }
  return zc::none;
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

namespace {

namespace incremental = driver::incremental_binding_query;
namespace graph_query = driver::module_graph_query;
namespace resolution_query = driver::incremental_module_resolution_query;

bool sameModuleKey(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameSourceKey(const identity::SourceFileKey& left, const identity::SourceFileKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameModulePath(zc::ArrayPtr<const identity::ModulePathSegment> left,
                    zc::ArrayPtr<const identity::ModulePathSegment> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].text() != right[index].text()) { return false; }
  }
  return true;
}

zc::Maybe<identity::CrateKey> decodeStableCrate(const incremental::StableCrateQueryKey& stable) {
  identity::CanonicalDecoder decoder(stable.canonicalCrateBytes());
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  if (crate == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(crate).encode().asPtr() != stable.canonicalCrateBytes()) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(crate));
}

zc::Maybe<incremental::CompilationRootSetQueryKey> completeContextRoots(
    const ModuleGraphMaterializationInput& input) {
  zc::Vector<identity::CrateKey> projected(input.coreInputs.projections().size());
  for (const auto& projection : input.coreInputs.projections()) {
    projected.add(projection.crate().clone());
  }
  return incremental::CompilationRootSetQueryKey::fromVerified(input.packageRequest,
                                                               projected.asPtr());
}

zc::Maybe<zc::Vector<identity::ModuleKey>> completeActiveModules(
    const query::QuerySnapshot& snapshot, const incremental::CompilationRootSetQueryKey& roots) {
  auto activeCrates = snapshot.get<incremental::ActiveCratesQuery>(roots);
  if (activeCrates.isRuntimeFailure() || activeCrates.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  zc::TreeMap<zc::String, identity::ModuleKey> canonical;
  for (const auto& stableCrate : activeCrates.value().crates()) {
    auto crate = decodeStableCrate(stableCrate);
    if (crate == zc::none) { return zc::none; }
    auto modules = snapshot.get<graph_query::ActiveModulesQuery>(ZC_ASSERT_NONNULL(crate));
    if (modules.isRuntimeFailure() || modules.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    for (const auto& module : modules.value().modules()) {
      auto key = zc::encodeHex(module.encode().asPtr());
      if (canonical.find(key) != zc::none) { return zc::none; }
      canonical.insert(zc::mv(key), module.clone());
    }
  }
  zc::Vector<identity::ModuleKey> result(canonical.size());
  for (auto& entry : canonical) { result.add(zc::mv(entry.value)); }
  return zc::mv(result);
}

bool sameModuleSequence(zc::ArrayPtr<const identity::ModuleKey> left,
                        zc::ArrayPtr<const identity::ModuleKey> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!sameModuleKey(left[index], right[index])) { return false; }
  }
  return true;
}

identity::ModuleDependencyKind dependencyKind(graph_query::DetachedModuleDependencySiteKind kind) {
  switch (kind) {
    case graph_query::DetachedModuleDependencySiteKind::Import:
      return identity::ModuleDependencyKind::Import;
    case graph_query::DetachedModuleDependencySiteKind::ForeignReexport:
      return identity::ModuleDependencyKind::ForeignReexport;
    case graph_query::DetachedModuleDependencySiteKind::ModuleAlias:
      return identity::ModuleDependencyKind::ModuleAlias;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<ast::NodeId> nodeAtSchemaOrdinal(const ast::Tree& tree, uint32_t expectedOrdinal) {
  zc::Maybe<ast::NodeId> result;
  uint32_t ordinal = 0;
  bool duplicate = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (ordinal == expectedOrdinal) {
      if (result != zc::none) {
        duplicate = true;
      } else {
        result = node;
      }
    }
    ++ordinal;
  });
  if (duplicate || result == zc::none) { return zc::none; }
  return result;
}

zc::Maybe<identity::ModuleDependencyKind> syntaxDependency(const ast::Tree& tree, ast::NodeId node,
                                                           ast::NodeId& path) {
  if (!tree.contains(node)) { return zc::none; }
  const auto& syntax = tree.node(node);
  if (syntax.kind == ast::SyntaxKind::ImportDeclaration) {
    path = ast::NodeId(syntax.payload.words[ast::kImportDeclarationPathWord]);
    return identity::ModuleDependencyKind::Import;
  }
  if (syntax.kind == ast::SyntaxKind::ExportDeclaration) {
    path = ast::NodeId(syntax.payload.words[ast::kExportDeclarationPathWord]);
    if (!tree.contains(path)) { return zc::none; }
    return identity::ModuleDependencyKind::ForeignReexport;
  }
  if (syntax.kind == ast::SyntaxKind::ModuleDeclaration &&
      static_cast<ast::ModuleDeclarationForm>(
          syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
          ast::ModuleDeclarationForm::Alias) {
    path = ast::NodeId(syntax.payload.words[ast::kModuleDeclarationAliasTargetWord]);
    return identity::ModuleDependencyKind::ModuleAlias;
  }
  return zc::none;
}

zc::Maybe<ModuleSyntaxDependencySite> materializeSite(
    const graph_query::DetachedModuleDependencySite& detached, const VerifiedParsedModule& parsed) {
  auto node = nodeAtSchemaOrdinal(parsed.tree(), detached.schemaPreorderOrdinal());
  if (node == zc::none) { return zc::none; }
  ast::NodeId path;
  auto kind = syntaxDependency(parsed.tree(), ZC_ASSERT_NONNULL(node), path);
  auto normalized = normalizedModulePath(parsed.tree(), path);
  auto span = parsed.spanFor(parsed.tree().node(ZC_ASSERT_NONNULL(node)).range);
  if (kind == zc::none || normalized == zc::none || span == zc::none ||
      ZC_ASSERT_NONNULL(kind) != dependencyKind(detached.kind()) ||
      !sameModulePath(ZC_ASSERT_NONNULL(normalized).asPtr(), detached.normalizedPath())) {
    return zc::none;
  }
  return ModuleSyntaxDependencySite(ZC_ASSERT_NONNULL(node), zc::mv(ZC_ASSERT_NONNULL(span)),
                                    detached.schemaPreorderOrdinal());
}

zc::Maybe<const ParsedModuleGraphInput&> parsedModuleFor(
    zc::ArrayPtr<const ParsedModuleGraphInput> parsedModules, identity::ModuleId module) {
  zc::Maybe<const ParsedModuleGraphInput&> result;
  for (const auto& parsed : parsedModules) {
    if (parsed.module != module) { continue; }
    if (result != zc::none) { return zc::none; }
    result = parsed;
  }
  return result;
}

zc::Maybe<identity::Sha256Digest> computeStableGraphRevision(
    const identity::SemanticContextFingerprint& fingerprint,
    const graph_query::ModuleGraphRecord& graph,
    zc::ArrayPtr<const VerifiedModuleDependencyEdge> edges) {
  zc::Vector<uint8_t> bytes;
  for (size_t index = 0; index < sizeof(kGraphDomain) - 1; ++index) {
    bytes.add(static_cast<uint8_t>(kGraphDomain[index]));
  }
  bytes.add(0x00);
  bytes.addAll(fingerprint.digest().bytes());
  const auto graphBytes = graph.encodeCanonical();
  appendUint64(bytes, graphBytes.size());
  bytes.addAll(graphBytes.asPtr());
  appendUint64(bytes, edges.size());
  zc::ArrayPtr<const uint8_t> prior;
  for (const auto& edge : edges) {
    if (prior.size() != 0 && !(prior < edge.encodedKey())) { return zc::none; }
    appendUint64(bytes, edge.encodedKey().size());
    bytes.addAll(edge.encodedKey());
    prior = edge.encodedKey();
  }
  return identity::sha256(bytes.asPtr());
}

bool projectedEdgesMatch(const graph_query::ModuleGraphRecord& graph,
                         zc::ArrayPtr<const identity::ModuleKey> modules,
                         zc::ArrayPtr<const identity::ModuleId> handles,
                         zc::ArrayPtr<const VerifiedModuleDependencyEdge> requestEdges) {
  zc::TreeMap<zc::String, graph_query::ModuleDependencyEdgeKey> projected;
  for (const auto& edge : requestEdges) {
    zc::Maybe<const identity::ModuleKey&> requester;
    zc::Maybe<const identity::ModuleKey&> dependency;
    for (size_t index = 0; index < handles.size(); ++index) {
      if (handles[index] == edge.request().requester()) { requester = modules[index]; }
      if (handles[index] == edge.target()) { dependency = modules[index]; }
    }
    if (requester == zc::none || dependency == zc::none) { return false; }
    auto stable = graph_query::ModuleDependencyEdgeKey::from(ZC_ASSERT_NONNULL(requester).clone(),
                                                             ZC_ASSERT_NONNULL(dependency).clone());
    if (stable == zc::none) { return false; }
    auto key = zc::encodeHex(ZC_ASSERT_NONNULL(stable).encodeCanonical().asPtr());
    if (projected.find(key) == zc::none) {
      projected.insert(zc::mv(key), zc::mv(ZC_ASSERT_NONNULL(stable)));
    }
  }
  if (projected.size() != graph.edges().size()) { return false; }
  size_t index = 0;
  for (const auto& entry : projected) {
    if (entry.value.encodeCanonical().asPtr() != graph.edges()[index++].encodeCanonical().asPtr()) {
      return false;
    }
  }
  return true;
}

zc::Maybe<incremental::CompilationRootSetQueryKey> reconstructVerifierContextRoots(
    const ModuleGraphMaterializationInput& input) {
  zc::Vector<incremental::CompilationRootKey> roots(input.packageRequest.roots().size() +
                                                    input.coreInputs.projections().size());
  for (const auto& root : input.packageRequest.roots()) {
    auto candidate = incremental::CompilationRootKey::userPackage(root.packageKey());
    if (candidate == zc::none) { return zc::none; }
    bool present = false;
    for (const auto& prior : roots) {
      if (prior.encodeCanonical().asPtr() ==
          ZC_ASSERT_NONNULL(candidate).encodeCanonical().asPtr()) {
        present = true;
        break;
      }
    }
    if (!present) { roots.add(zc::mv(ZC_ASSERT_NONNULL(candidate))); }
  }
  for (const auto& projection : input.coreInputs.projections()) {
    auto candidate = incremental::CompilationRootKey::toolchainCore(projection.crate());
    if (candidate == zc::none) { return zc::none; }
    for (const auto& prior : roots) {
      if (prior.encodeCanonical().asPtr() ==
          ZC_ASSERT_NONNULL(candidate).encodeCanonical().asPtr()) {
        return zc::none;
      }
    }
    roots.add(zc::mv(ZC_ASSERT_NONNULL(candidate)));
  }
  return incremental::CompilationRootSetQueryKey::from(zc::mv(roots));
}

zc::Maybe<zc::Vector<identity::ModuleKey>> demandVerifierActiveModules(
    const query::QuerySnapshot& snapshot, const incremental::CompilationRootSetQueryKey& roots) {
  auto activeCrates = snapshot.get<incremental::ActiveCratesQuery>(roots);
  if (activeCrates.isRuntimeFailure() || activeCrates.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  zc::TreeMap<zc::String, identity::ModuleKey> ordered;
  for (const auto& stableCrate : activeCrates.value().crates()) {
    identity::CanonicalDecoder decoder(stableCrate.canonicalCrateBytes());
    auto crate = identity::CrateKey::decodeCanonical(decoder);
    if (crate == zc::none || !decoder.finished() ||
        ZC_ASSERT_NONNULL(crate).encode().asPtr() != stableCrate.canonicalCrateBytes()) {
      return zc::none;
    }
    auto active = snapshot.get<graph_query::ActiveModulesQuery>(ZC_ASSERT_NONNULL(crate));
    if (active.isRuntimeFailure() || active.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    for (const auto& module : active.value().modules()) {
      auto bytes = zc::encodeHex(module.encode().asPtr());
      if (ordered.find(bytes) != zc::none) { return zc::none; }
      ordered.insert(zc::mv(bytes), module.clone());
    }
  }
  zc::Vector<identity::ModuleKey> modules(ordered.size());
  for (auto& entry : ordered) { modules.add(zc::mv(entry.value)); }
  return zc::mv(modules);
}

bool verifierModuleSequenceEquals(zc::ArrayPtr<const identity::ModuleKey> left,
                                  zc::ArrayPtr<const identity::ModuleKey> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].encode().asPtr() != right[index].encode().asPtr()) { return false; }
  }
  return true;
}

identity::ModuleDependencyKind verifierDependencyKind(
    graph_query::DetachedModuleDependencySiteKind kind) {
  switch (kind) {
    case graph_query::DetachedModuleDependencySiteKind::Import:
      return identity::ModuleDependencyKind::Import;
    case graph_query::DetachedModuleDependencySiteKind::ForeignReexport:
      return identity::ModuleDependencyKind::ForeignReexport;
    case graph_query::DetachedModuleDependencySiteKind::ModuleAlias:
      return identity::ModuleDependencyKind::ModuleAlias;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<ModuleSyntaxDependencySite> rebuildVerifierSite(
    const graph_query::DetachedModuleDependencySite& detached, const VerifiedParsedModule& parsed) {
  zc::Maybe<ast::NodeId> selectedNode;
  uint32_t ordinal = 0;
  ast::visitTreePreOrder(parsed.tree(), parsed.tree().root(),
                         [&](ast::NodeId node, const ast::Node&) {
                           if (ordinal == detached.schemaPreorderOrdinal()) { selectedNode = node; }
                           ++ordinal;
                         });
  if (selectedNode == zc::none) { return zc::none; }

  const auto& syntax = parsed.tree().node(ZC_ASSERT_NONNULL(selectedNode));
  zc::Maybe<identity::ModuleDependencyKind> kind;
  ast::NodeId path;
  if (syntax.kind == ast::SyntaxKind::ImportDeclaration) {
    kind = identity::ModuleDependencyKind::Import;
    path = ast::NodeId(syntax.payload.words[ast::kImportDeclarationPathWord]);
  } else if (syntax.kind == ast::SyntaxKind::ExportDeclaration) {
    kind = identity::ModuleDependencyKind::ForeignReexport;
    path = ast::NodeId(syntax.payload.words[ast::kExportDeclarationPathWord]);
  } else if (syntax.kind == ast::SyntaxKind::ModuleDeclaration &&
             static_cast<ast::ModuleDeclarationForm>(
                 syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
                 ast::ModuleDeclarationForm::Alias) {
    kind = identity::ModuleDependencyKind::ModuleAlias;
    path = ast::NodeId(syntax.payload.words[ast::kModuleDeclarationAliasTargetWord]);
  }
  if (kind == zc::none || !parsed.tree().contains(path) ||
      ZC_ASSERT_NONNULL(kind) != verifierDependencyKind(detached.kind())) {
    return zc::none;
  }
  auto normalized = normalizedModulePath(parsed.tree(), path);
  auto span = parsed.spanFor(syntax.range);
  if (normalized == zc::none || span == zc::none ||
      !sameModulePath(ZC_ASSERT_NONNULL(normalized).asPtr(), detached.normalizedPath())) {
    return zc::none;
  }
  return ModuleSyntaxDependencySite(ZC_ASSERT_NONNULL(selectedNode),
                                    zc::mv(ZC_ASSERT_NONNULL(span)),
                                    detached.schemaPreorderOrdinal());
}

bool verifierGraphProjectionMatches(const graph_query::ModuleGraphRecord& graph,
                                    zc::ArrayPtr<const identity::ModuleKey> modules,
                                    zc::ArrayPtr<const identity::ModuleId> handles,
                                    zc::ArrayPtr<const VerifiedModuleDependencyEdge> requestEdges) {
  zc::TreeMap<zc::String, graph_query::ModuleDependencyEdgeKey> expected;
  for (const auto& requestEdge : requestEdges) {
    zc::Maybe<const identity::ModuleKey&> requester;
    zc::Maybe<const identity::ModuleKey&> dependency;
    for (size_t index = 0; index < handles.size(); ++index) {
      if (handles[index] == requestEdge.request().requester()) { requester = modules[index]; }
      if (handles[index] == requestEdge.target()) { dependency = modules[index]; }
    }
    if (requester == zc::none || dependency == zc::none) { return false; }
    auto edge = graph_query::ModuleDependencyEdgeKey::from(ZC_ASSERT_NONNULL(requester).clone(),
                                                           ZC_ASSERT_NONNULL(dependency).clone());
    if (edge == zc::none) { return false; }
    auto bytes = ZC_ASSERT_NONNULL(edge).encodeCanonical();
    auto key = zc::encodeHex(bytes.asPtr());
    if (expected.find(key) == zc::none) {
      expected.insert(zc::mv(key), zc::mv(ZC_ASSERT_NONNULL(edge)));
    }
  }
  if (expected.size() != graph.edges().size()) { return false; }
  size_t graphIndex = 0;
  for (const auto& entry : expected) {
    if (entry.value.encodeCanonical().asPtr() !=
        graph.edges()[graphIndex].encodeCanonical().asPtr()) {
      return false;
    }
    ++graphIndex;
  }
  return true;
}

zc::Maybe<identity::Sha256Digest> recomputeVerifierGraphRevision(
    const identity::SemanticContextFingerprint& fingerprint,
    const graph_query::ModuleGraphRecord& graph,
    zc::ArrayPtr<const VerifiedModuleDependencyEdge> requestEdges) {
  zc::Vector<uint8_t> preimage;
  for (size_t index = 0; index < sizeof(kGraphDomain) - 1; ++index) {
    preimage.add(static_cast<uint8_t>(kGraphDomain[index]));
  }
  preimage.add(0x00);
  preimage.addAll(fingerprint.digest().bytes());
  const auto graphBytes = graph.encodeCanonical();
  appendUint64(preimage, graphBytes.size());
  preimage.addAll(graphBytes.asPtr());
  appendUint64(preimage, requestEdges.size());
  zc::ArrayPtr<const uint8_t> previous;
  for (const auto& edge : requestEdges) {
    if (previous.size() != 0 && !(previous < edge.encodedKey())) { return zc::none; }
    appendUint64(preimage, edge.encodedKey().size());
    preimage.addAll(edge.encodedKey());
    previous = edge.encodedKey();
  }
  return identity::sha256(preimage.asPtr());
}

}  // namespace

struct BinderModuleGraphCandidate::Impl final {
  Impl(incremental::CompilationRootSetQueryKey&& expectedContextRoots,
       identity::SemanticContextBrand context, identity::SemanticContextFingerprint&& fingerprint,
       graph_query::ModuleGraphRecord&& stableGraph, graph_query::ModuleGraphSccRecord&& stableScc,
       zc::Vector<identity::ModuleKey>&& modules, zc::Vector<identity::ModuleId>&& handles,
       zc::Vector<identity::SourceFileKey>&& sources,
       zc::Vector<VerifiedModuleDependencyEdge>&& requestEdges,
       ModuleGraphRevision revision) noexcept
      : expectedContextRoots(zc::mv(expectedContextRoots)),
        context(context),
        fingerprint(zc::mv(fingerprint)),
        stableGraph(zc::mv(stableGraph)),
        stableScc(zc::mv(stableScc)),
        modules(zc::mv(modules)),
        handles(zc::mv(handles)),
        sources(zc::mv(sources)),
        requestEdges(zc::mv(requestEdges)),
        revision(revision) {}

  incremental::CompilationRootSetQueryKey expectedContextRoots;
  identity::SemanticContextBrand context;
  identity::SemanticContextFingerprint fingerprint;
  graph_query::ModuleGraphRecord stableGraph;
  graph_query::ModuleGraphSccRecord stableScc;
  zc::Vector<identity::ModuleKey> modules;
  zc::Vector<identity::ModuleId> handles;
  zc::Vector<identity::SourceFileKey> sources;
  zc::Vector<VerifiedModuleDependencyEdge> requestEdges;
  ModuleGraphRevision revision;
};

BinderModuleGraphCandidate::BinderModuleGraphCandidate(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
BinderModuleGraphCandidate::~BinderModuleGraphCandidate() noexcept(false) = default;
BinderModuleGraphCandidate::BinderModuleGraphCandidate(BinderModuleGraphCandidate&&) noexcept =
    default;
BinderModuleGraphCandidate& BinderModuleGraphCandidate::operator=(
    BinderModuleGraphCandidate&&) noexcept = default;

ModuleGraphCandidateResult VerifiedModuleGraphBuilder::produce(
    const ModuleGraphMaterializationInput& input) {
  if (!input.semanticContext.isValid() || !allRegistriesFrozen(input.registries) ||
      input.parsedModules.size() != input.stableGraph.modules().size()) {
    return failure(ModuleGraphInvariantKind::InputMismatch);
  }
  auto expectedRoots = completeContextRoots(input);
  if (expectedRoots == zc::none) { return failure(ModuleGraphInvariantKind::InputMismatch); }
  auto activeModules = completeActiveModules(input.finalSnapshot, ZC_ASSERT_NONNULL(expectedRoots));
  auto graph =
      input.finalSnapshot.get<graph_query::ModuleGraphQuery>(ZC_ASSERT_NONNULL(expectedRoots));
  auto scc =
      input.finalSnapshot.get<graph_query::ModuleGraphSccQuery>(ZC_ASSERT_NONNULL(expectedRoots));
  if (activeModules == zc::none || graph.isRuntimeFailure() || scc.isRuntimeFailure() ||
      graph.kind() != query::QueryValueKind::Value || scc.kind() != query::QueryValueKind::Value ||
      !sameModuleSequence(ZC_ASSERT_NONNULL(activeModules).asPtr(), graph.value().modules()) ||
      graph.value().encodeCanonical().asPtr() != input.stableGraph.encodeCanonical().asPtr() ||
      scc.value().encodeCanonical().asPtr() != input.stableScc.encodeCanonical().asPtr() ||
      scc.value().hasCycle(graph.value())) {
    return failure(ModuleGraphInvariantKind::InvalidEdge);
  }
  auto fingerprint = identity::SemanticContextFingerprint::compute(
      input.registries, input.toolchainInputs, input.packageEdges, input.crateEdges);
  if (fingerprint == zc::none ||
      ZC_ASSERT_NONNULL(fingerprint).digest() != input.semanticContextFingerprint.digest()) {
    return failure(ModuleGraphInvariantKind::RevisionMismatch);
  }

  zc::Vector<identity::ModuleKey> modules(input.stableGraph.modules().size());
  zc::Vector<identity::ModuleId> handles(input.stableGraph.modules().size());
  zc::Vector<identity::SourceFileKey> sources(input.stableGraph.modules().size());
  zc::Vector<VerifiedModuleDependencyEdge> requestEdges;
  for (const auto& module : input.stableGraph.modules()) {
    auto handle = input.registries.modules().find(module);
    auto selected = input.finalSnapshot.get<graph_query::SelectedModuleSourceQuery>(module);
    if (handle == zc::none || selected.isRuntimeFailure() ||
        selected.kind() != query::QueryValueKind::Value) {
      return failure(ModuleGraphInvariantKind::InputMismatch);
    }
    auto sourceHandle = input.registries.sourceFiles().find(selected.value());
    auto parsed = parsedModuleFor(input.parsedModules, ZC_ASSERT_NONNULL(handle));
    if (sourceHandle == zc::none || parsed == zc::none ||
        !sameSourceKey(ZC_ASSERT_NONNULL(parsed).parsedModule.source(), selected.value())) {
      return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
    }
    auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
    if (sourceKey == zc::none) {
      return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
    }
    auto finalParse =
        input.finalSnapshot.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
    if (finalParse.isRuntimeFailure() || finalParse.kind() != query::QueryValueKind::Value ||
        finalParse.value().capability().canonicalSourceKey() != selected.value().encode().asPtr() ||
        finalParse.value().capability().contentDigest() !=
            ZC_ASSERT_NONNULL(parsed).parsedModule.contentDigest() ||
        finalParse.value().capability().sourceBytes().size() !=
            ZC_ASSERT_NONNULL(parsed).parsedModule.byteLength()) {
      return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
    }
    auto sites = input.finalSnapshot.get<graph_query::ModuleDependencySitesQuery>(module);
    auto requests = input.finalSnapshot.get<graph_query::ModuleDependencyRequestsQuery>(module);
    if (sites.isRuntimeFailure() || requests.isRuntimeFailure() ||
        sites.kind() != query::QueryValueKind::Value ||
        requests.kind() != query::QueryValueKind::Value ||
        !sameSourceKey(sites.value().source(), selected.value()) ||
        sites.value().sourceDigest() != ZC_ASSERT_NONNULL(parsed).parsedModule.contentDigest()) {
      return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
    }
    zc::Vector<uint8_t> consumedSites(sites.value().sites().size());
    consumedSites.resize(sites.value().sites().size());
    for (auto& consumed : consumedSites) { consumed = 0; }
    for (const auto& requestKey : requests.value().requests()) {
      if (!sameModuleKey(requestKey.requester(), module)) {
        return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
      }
      auto resolution =
          input.finalSnapshot.get<resolution_query::ResolveModuleRequestQuery>(requestKey);
      if (resolution.isRuntimeFailure() || resolution.kind() != query::QueryValueKind::Value ||
          resolution.value().candidates().size() != 1) {
        return failure(ModuleGraphInvariantKind::IncompleteResolution, ZC_ASSERT_NONNULL(handle));
      }
      const auto& targetKey = resolution.value().candidates()[0];
      auto target = input.registries.modules().find(targetKey);
      if (target == zc::none) {
        return failure(ModuleGraphInvariantKind::InvalidEdge, ZC_ASSERT_NONNULL(handle));
      }
      zc::Maybe<ModuleDependencyRequest> request;
      if (requestKey.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
        auto configured = input.finalSnapshot.probeInput<resolution_query::ConfiguredPreludeInput>(
            module.crate());
        auto selectedTarget = configured.kind() == query::QueryValueKind::Value
                                  ? configured.value().target()
                                  : zc::Maybe<const identity::ModuleKey&>();
        if (configured.isRuntimeFailure() || selectedTarget == zc::none ||
            !sameModuleKey(ZC_ASSERT_NONNULL(selectedTarget), targetKey)) {
          return failure(ModuleGraphInvariantKind::InvalidPrelude, ZC_ASSERT_NONNULL(handle));
        }
        request = ModuleDependencyRequest::prelude(ZC_ASSERT_NONNULL(handle), requestKey.clone(),
                                                   targetKey.clone());
      } else {
        zc::Vector<ModuleSyntaxDependencySite> syntaxSites;
        auto requestPath = requestKey.normalizedPath();
        if (requestPath == zc::none) {
          return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
        }
        for (size_t siteIndex = 0; siteIndex < sites.value().sites().size(); ++siteIndex) {
          const auto& site = sites.value().sites()[siteIndex];
          if (dependencyKind(site.kind()) != requestKey.dependencyKind() ||
              !sameModulePath(site.normalizedPath(), ZC_ASSERT_NONNULL(requestPath))) {
            continue;
          }
          if (consumedSites[siteIndex] != 0) {
            return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
          }
          auto materialized = materializeSite(site, ZC_ASSERT_NONNULL(parsed).parsedModule);
          if (materialized == zc::none) {
            return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
          }
          consumedSites[siteIndex] = 1;
          syntaxSites.add(zc::mv(ZC_ASSERT_NONNULL(materialized)));
        }
        request = ModuleDependencyRequest::source(ZC_ASSERT_NONNULL(handle), requestKey.clone(),
                                                  zc::mv(syntaxSites));
      }
      if (request == zc::none) {
        return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
      }
      auto encoded = encodeDependencyEdgeKey(module, ZC_ASSERT_NONNULL(request), targetKey);
      requestEdges.add(VerifiedModuleDependencyEdge(zc::mv(ZC_ASSERT_NONNULL(request)),
                                                    ZC_ASSERT_NONNULL(target), zc::mv(encoded)));
    }
    for (const auto consumed : consumedSites) {
      if (consumed == 0) {
        return failure(ModuleGraphInvariantKind::IncompleteResolution, ZC_ASSERT_NONNULL(handle));
      }
    }
    modules.add(module.clone());
    handles.add(ZC_ASSERT_NONNULL(handle));
    sources.add(selected.value().clone());
  }

  for (size_t index = 1; index < requestEdges.size(); ++index) {
    auto current = zc::mv(requestEdges[index]);
    size_t insertion = index;
    while (insertion != 0 && current.encodedKey() < requestEdges[insertion - 1].encodedKey()) {
      requestEdges[insertion] = zc::mv(requestEdges[insertion - 1]);
      --insertion;
    }
    requestEdges[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < requestEdges.size(); ++index) {
    if (requestEdges[index - 1].encodedKey() == requestEdges[index].encodedKey()) {
      return failure(ModuleGraphInvariantKind::InvalidEdge);
    }
  }
  if (!projectedEdgesMatch(input.stableGraph, modules.asPtr(), handles.asPtr(),
                           requestEdges.asPtr())) {
    return failure(ModuleGraphInvariantKind::InvalidEdge);
  }
  auto revision = computeStableGraphRevision(ZC_ASSERT_NONNULL(fingerprint), input.stableGraph,
                                             requestEdges.asPtr());
  if (revision == zc::none) { return failure(ModuleGraphInvariantKind::RevisionMismatch); }
  BinderModuleGraphCandidate candidate(zc::heap<BinderModuleGraphCandidate::Impl>(
      zc::mv(ZC_ASSERT_NONNULL(expectedRoots)), input.semanticContext,
      zc::mv(ZC_ASSERT_NONNULL(fingerprint)), input.stableGraph.clone(), input.stableScc.clone(),
      zc::mv(modules), zc::mv(handles), zc::mv(sources), zc::mv(requestEdges),
      ModuleGraphRevision(ZC_ASSERT_NONNULL(revision))));
  return zc::mv(candidate);
}

ModuleGraphMaterializationResult VerifiedModuleGraphBuilder::build(
    const ModuleGraphMaterializationInput& input) {
  auto candidate = produce(input);
  if (candidate.is<ModuleGraphInvariantFact>()) {
    return zc::mv(candidate.get<ModuleGraphInvariantFact>());
  }
  return VerifiedModuleGraphVerifier::verify(input, candidate.get<BinderModuleGraphCandidate>());
}

ModuleGraphMaterializationResult VerifiedModuleGraphVerifier::verify(
    const ModuleGraphMaterializationInput& input, const BinderModuleGraphCandidate& candidate) {
  const auto& value = *candidate.impl;
  auto expectedRoots = reconstructVerifierContextRoots(input);
  auto activeModules =
      expectedRoots == zc::none
          ? zc::Maybe<zc::Vector<identity::ModuleKey>>()
          : demandVerifierActiveModules(input.finalSnapshot, ZC_ASSERT_NONNULL(expectedRoots));
  if (expectedRoots == zc::none || activeModules == zc::none ||
      ZC_ASSERT_NONNULL(expectedRoots).encodeCanonical().asPtr() !=
          value.expectedContextRoots.encodeCanonical().asPtr() ||
      !verifierModuleSequenceEquals(ZC_ASSERT_NONNULL(activeModules).asPtr(),
                                    input.stableGraph.modules()) ||
      !verifierModuleSequenceEquals(value.modules.asPtr(), input.stableGraph.modules()) ||
      value.modules.size() != value.handles.size() ||
      value.modules.size() != value.sources.size() || value.context != input.semanticContext) {
    return failure(ModuleGraphInvariantKind::InputMismatch);
  }
  auto graph =
      input.finalSnapshot.get<graph_query::ModuleGraphQuery>(ZC_ASSERT_NONNULL(expectedRoots));
  auto scc =
      input.finalSnapshot.get<graph_query::ModuleGraphSccQuery>(ZC_ASSERT_NONNULL(expectedRoots));
  if (graph.isRuntimeFailure() || scc.isRuntimeFailure() ||
      graph.kind() != query::QueryValueKind::Value || scc.kind() != query::QueryValueKind::Value ||
      graph.value().encodeCanonical().asPtr() != input.stableGraph.encodeCanonical().asPtr() ||
      graph.value().encodeCanonical().asPtr() != value.stableGraph.encodeCanonical().asPtr() ||
      scc.value().encodeCanonical().asPtr() != input.stableScc.encodeCanonical().asPtr() ||
      scc.value().encodeCanonical().asPtr() != value.stableScc.encodeCanonical().asPtr() ||
      scc.value().hasCycle(graph.value())) {
    return failure(ModuleGraphInvariantKind::InvalidEdge);
  }
  auto fingerprint = identity::SemanticContextFingerprint::compute(
      input.registries, input.toolchainInputs, input.packageEdges, input.crateEdges);
  if (fingerprint == zc::none ||
      ZC_ASSERT_NONNULL(fingerprint).digest() != input.semanticContextFingerprint.digest() ||
      ZC_ASSERT_NONNULL(fingerprint).digest() != value.fingerprint.digest()) {
    return failure(ModuleGraphInvariantKind::RevisionMismatch);
  }

  for (size_t edgeIndex = 0; edgeIndex < value.requestEdges.size(); ++edgeIndex) {
    const auto& edge = value.requestEdges[edgeIndex];
    if (edgeIndex != 0 && !(value.requestEdges[edgeIndex - 1].encodedKey() < edge.encodedKey())) {
      return failure(ModuleGraphInvariantKind::InvalidEdge, edge.request().requester());
    }
  }
  size_t expectedRequestCount = 0;
  for (size_t moduleIndex = 0; moduleIndex < value.modules.size(); ++moduleIndex) {
    const auto& module = value.modules[moduleIndex];
    const auto handle = input.registries.modules().find(module);
    auto selected = input.finalSnapshot.get<graph_query::SelectedModuleSourceQuery>(module);
    if (handle == zc::none || selected.isRuntimeFailure() ||
        selected.kind() != query::QueryValueKind::Value ||
        ZC_ASSERT_NONNULL(handle) != value.handles[moduleIndex] ||
        !sameSourceKey(selected.value(), value.sources[moduleIndex]) ||
        input.registries.sourceFiles().find(selected.value()) == zc::none) {
      return failure(ModuleGraphInvariantKind::InputMismatch);
    }
    const auto parsed = parsedModuleFor(input.parsedModules, ZC_ASSERT_NONNULL(handle));
    const auto sourceKey =
        identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
    if (parsed == zc::none || sourceKey == zc::none ||
        !sameSourceKey(ZC_ASSERT_NONNULL(parsed).parsedModule.source(), selected.value())) {
      return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
    }
    auto finalParse =
        input.finalSnapshot.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
    if (finalParse.isRuntimeFailure() || finalParse.kind() != query::QueryValueKind::Value ||
        finalParse.value().capability().canonicalSourceKey() != selected.value().encode().asPtr() ||
        finalParse.value().capability().contentDigest() !=
            ZC_ASSERT_NONNULL(parsed).parsedModule.contentDigest() ||
        finalParse.value().capability().sourceBytes().size() !=
            ZC_ASSERT_NONNULL(parsed).parsedModule.byteLength()) {
      return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
    }

    auto sites = input.finalSnapshot.get<graph_query::ModuleDependencySitesQuery>(module);
    auto requests = input.finalSnapshot.get<graph_query::ModuleDependencyRequestsQuery>(module);
    if (sites.isRuntimeFailure() || requests.isRuntimeFailure() ||
        sites.kind() != query::QueryValueKind::Value ||
        requests.kind() != query::QueryValueKind::Value ||
        !sameSourceKey(sites.value().source(), selected.value()) ||
        sites.value().sourceDigest() != ZC_ASSERT_NONNULL(parsed).parsedModule.contentDigest()) {
      return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
    }
    expectedRequestCount += requests.value().requests().size();
    zc::Vector<uint8_t> consumedSites(sites.value().sites().size());
    consumedSites.resize(sites.value().sites().size());
    for (auto& consumed : consumedSites) { consumed = 0; }

    for (const auto& requestKey : requests.value().requests()) {
      zc::Maybe<const VerifiedModuleDependencyEdge&> candidateEdge;
      for (const auto& edge : value.requestEdges) {
        if (edge.request().requester() != ZC_ASSERT_NONNULL(handle) ||
            edge.request().key().encode().asPtr() != requestKey.encode().asPtr()) {
          continue;
        }
        if (candidateEdge != zc::none) {
          return failure(ModuleGraphInvariantKind::InvalidEdge, ZC_ASSERT_NONNULL(handle));
        }
        candidateEdge = edge;
      }
      if (candidateEdge == zc::none) {
        return failure(ModuleGraphInvariantKind::IncompleteResolution, ZC_ASSERT_NONNULL(handle));
      }
      const auto& edge = ZC_ASSERT_NONNULL(candidateEdge);
      zc::Maybe<const identity::ModuleKey&> target;
      for (size_t targetIndex = 0; targetIndex < value.handles.size(); ++targetIndex) {
        if (value.handles[targetIndex] == edge.target()) { target = value.modules[targetIndex]; }
      }
      auto resolution =
          input.finalSnapshot.get<resolution_query::ResolveModuleRequestQuery>(requestKey);
      if (target == zc::none || resolution.isRuntimeFailure() ||
          resolution.kind() != query::QueryValueKind::Value ||
          resolution.value().candidates().size() != 1 ||
          !sameModuleKey(resolution.value().candidates()[0], ZC_ASSERT_NONNULL(target)) ||
          encodeDependencyEdgeKey(module, edge.request(), ZC_ASSERT_NONNULL(target)).asPtr() !=
              edge.encodedKey()) {
        return failure(ModuleGraphInvariantKind::InvalidEdge, ZC_ASSERT_NONNULL(handle));
      }
      if (requestKey.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
        auto configured = input.finalSnapshot.probeInput<resolution_query::ConfiguredPreludeInput>(
            module.crate());
        auto configuredTarget = configured.kind() == query::QueryValueKind::Value
                                    ? configured.value().target()
                                    : zc::Maybe<const identity::ModuleKey&>();
        if (!edge.request().isPrelude() || configured.isRuntimeFailure() ||
            configuredTarget == zc::none ||
            !sameModuleKey(ZC_ASSERT_NONNULL(configuredTarget), ZC_ASSERT_NONNULL(target)) ||
            !sameModuleKey(edge.request().requestedTarget(), ZC_ASSERT_NONNULL(target)) ||
            edge.request().syntaxSites().size() != 0) {
          return failure(ModuleGraphInvariantKind::InvalidPrelude, ZC_ASSERT_NONNULL(handle));
        }
        continue;
      }
      if (edge.request().isPrelude() || edge.request().syntaxSites().size() == 0) {
        return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
      }
      const auto requestPath = requestKey.normalizedPath();
      if (requestPath == zc::none) {
        return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
      }
      for (const auto& syntaxSite : edge.request().syntaxSites()) {
        zc::Maybe<size_t> detachedIndex;
        for (size_t siteIndex = 0; siteIndex < sites.value().sites().size(); ++siteIndex) {
          if (sites.value().sites()[siteIndex].schemaPreorderOrdinal() ==
              syntaxSite.schemaPreorderOrdinal) {
            if (detachedIndex != zc::none) {
              return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
            }
            detachedIndex = siteIndex;
          }
        }
        if (detachedIndex == zc::none || consumedSites[ZC_ASSERT_NONNULL(detachedIndex)] != 0) {
          return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
        }
        const auto& detached = sites.value().sites()[ZC_ASSERT_NONNULL(detachedIndex)];
        if (verifierDependencyKind(detached.kind()) != edge.request().kind() ||
            !sameModulePath(detached.normalizedPath(), ZC_ASSERT_NONNULL(requestPath))) {
          return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
        }
        auto rebuilt = rebuildVerifierSite(detached, ZC_ASSERT_NONNULL(parsed).parsedModule);
        if (rebuilt == zc::none || ZC_ASSERT_NONNULL(rebuilt).node != syntaxSite.node ||
            ZC_ASSERT_NONNULL(rebuilt).span.byteStart() != syntaxSite.span.byteStart() ||
            ZC_ASSERT_NONNULL(rebuilt).span.byteEnd() != syntaxSite.span.byteEnd()) {
          return failure(ModuleGraphInvariantKind::InputMismatch, ZC_ASSERT_NONNULL(handle));
        }
        consumedSites[ZC_ASSERT_NONNULL(detachedIndex)] = 1;
      }
    }
    for (const auto consumed : consumedSites) {
      if (consumed == 0) {
        return failure(ModuleGraphInvariantKind::IncompleteResolution, ZC_ASSERT_NONNULL(handle));
      }
    }
  }
  if (expectedRequestCount != value.requestEdges.size()) {
    return failure(ModuleGraphInvariantKind::IncompleteResolution);
  }
  if (!verifierGraphProjectionMatches(input.stableGraph, value.modules.asPtr(),
                                      value.handles.asPtr(), value.requestEdges.asPtr())) {
    return failure(ModuleGraphInvariantKind::InvalidEdge);
  }
  auto revision = recomputeVerifierGraphRevision(ZC_ASSERT_NONNULL(fingerprint), input.stableGraph,
                                                 value.requestEdges.asPtr());
  if (revision == zc::none || ZC_ASSERT_NONNULL(revision) != value.revision.digest()) {
    return failure(ModuleGraphInvariantKind::RevisionMismatch);
  }
  zc::Vector<identity::ModuleKey> modules(value.modules.size());
  for (const auto& module : value.modules) { modules.add(module.clone()); }
  zc::Vector<identity::ModuleId> handles(value.handles.size());
  for (const auto handle : value.handles) { handles.add(handle); }
  zc::Vector<identity::SourceFileKey> sources(value.sources.size());
  for (const auto& source : value.sources) { sources.add(source.clone()); }
  zc::Vector<VerifiedModuleDependencyEdge> edges(value.requestEdges.size());
  for (const auto& edge : value.requestEdges) {
    edges.add(VerifiedModuleDependencyEdge(edge.request().clone(), edge.target(),
                                           zc::heapArray<uint8_t>(edge.encodedKey())));
  }
  return VerifiedModuleGraph(zc::heap<VerifiedModuleGraph::Impl>(
      value.context, zc::mv(ZC_ASSERT_NONNULL(fingerprint)), zc::mv(modules), zc::mv(handles),
      zc::mv(sources), zc::mv(edges), ModuleGraphRevision(ZC_ASSERT_NONNULL(revision))));
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
  Impl(const BindingInputCandidate& candidate,
       identity::CompilationUnitIdentity&& compilationUnitKey, identity::CrateKey&& crateKey,
       identity::ModuleKey&& moduleKey, identity::SemanticContextFingerprint&& semanticFingerprint,
       zc::Vector<VerifiedExportSurfaceView>&& dependencySurfaces,
       zc::Maybe<VerifiedExportSurfaceView>&& preludeSurface,
       zc::Vector<ResolvedImportEdge>&& resolvedImports,
       zc::Vector<ResolvedModuleAlias>&& resolvedModuleAliases,
       zc::Vector<ResolvedLocalExportSpecifier>&& localExportSpecifiers)
      : semanticContext(candidate.semanticContext),
        compilationUnit(candidate.compilationUnit),
        compilationUnitKey(zc::mv(compilationUnitKey)),
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
  identity::CompilationUnitId compilationUnit;
  identity::CompilationUnitIdentity compilationUnitKey;
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
identity::CompilationUnitId VerifiedBindingInput::compilationUnit() const noexcept {
  return impl->compilationUnit;
}
const identity::CompilationUnitIdentity& VerifiedBindingInput::compilationUnitKey() const noexcept {
  return impl->compilationUnitKey;
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
  const auto inputFailure = [&]() {
    return failure(ModuleGraphInvariantKind::InputMismatch, candidate.module);
  };
  const auto incomplete = [&]() {
    return failure(ModuleGraphInvariantKind::IncompleteResolution, candidate.module);
  };
  if (!candidate.semanticContext.isValid() || !allRegistriesFrozen(registries) ||
      !candidate.compilationUnit.belongsTo(candidate.semanticContext) ||
      !candidate.crate.belongsTo(candidate.semanticContext) ||
      !candidate.module.belongsTo(candidate.semanticContext) ||
      !candidate.parsedModule.sourceFile().belongsTo(candidate.semanticContext) ||
      candidate.moduleGraph.semanticContext() != candidate.semanticContext ||
      candidate.moduleGraph.requester() != candidate.module ||
      candidate.definitions.semanticContext() != candidate.semanticContext ||
      candidate.definitions.module() != candidate.module) {
    return inputFailure();
  }
  auto compilationUnit = registries.compilationUnits().lookup(candidate.compilationUnit);
  auto crate = registries.crates().lookup(candidate.crate);
  auto source = registries.sourceFiles().lookup(candidate.parsedModule.sourceFile());
  auto module = registries.modules().lookup(candidate.module);
  if (compilationUnit == zc::none || crate == zc::none || source == zc::none ||
      module == zc::none ||
      registries.sourceSnapshot(candidate.parsedModule.sourceFile()) == zc::none) {
    return inputFailure();
  }
  zc::Maybe<identity::CompilationUnitIdentity> verifiedCompilationUnit;
  zc::Maybe<identity::CrateKey> verifiedCrate;
  zc::Maybe<identity::ModuleKey> verifiedModule;
  zc::Maybe<identity::SemanticContextFingerprint> verifiedFingerprint;
  ZC_IF_SOME(compilationUnitValue, compilationUnit) {
    ZC_IF_SOME(crateValue, crate) {
      ZC_IF_SOME(sourceValue, source) {
        ZC_IF_SOME(moduleValue, module) {
          if (compilationUnitValue.encode() != crateValue.unit().encode() ||
              !sourceValue.belongsTo(crateValue) ||
              moduleValue.crate().encode() != crateValue.encode() ||
              !candidate.parsedModule.source().sameAs(sourceValue)) {
            return inputFailure();
          }
          const auto& fingerprintValue = candidate.moduleGraph.semanticFingerprint();
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
          verifiedCompilationUnit = compilationUnitValue.clone();
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

  ZC_IF_SOME(compilationUnitValue, verifiedCompilationUnit) {
    ZC_IF_SOME(crateValue, verifiedCrate) {
      ZC_IF_SOME(moduleValue, verifiedModule) {
        ZC_IF_SOME(fingerprintValue, verifiedFingerprint) {
          return VerifiedBindingInput(zc::heap<VerifiedBindingInput::Impl>(
              candidate, zc::mv(compilationUnitValue), zc::mv(crateValue), zc::mv(moduleValue),
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
