// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-candidate-codec.h"

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/import-binding.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

int compareCanonicalBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t count = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}
bool encodeScopeId(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                   ScopeId id) {
  if (id.module() != input.module() || !id.belongsTo(input.semanticContext())) { return false; }
  input.moduleKey().encode(encoder);
  encoder.encodeUint32(id.index());
  return true;
}

bool encodeDefinition(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      identity::DefId definition) {
  ZC_IF_SOME(key, input.definitionKey(definition)) {
    key.encode(encoder);
    return true;
  }
  return false;
}

bool encodeAnonymous(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                     const AnonymousOwnerLocalKey& anonymous) {
  for (const auto& entry : input.definitions().anonymousEntities()) {
    if (entry.key == anonymous) {
      anonymous.encode(encoder);
      return true;
    }
  }
  return false;
}

bool encodeLabelOwner(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const LabelOwner& owner) {
  const auto& value = owner.value();
  if (value.is<ModuleLabelOwner>()) {
    if (value.get<ModuleLabelOwner>().module != input.module()) { return false; }
    encoder.encodeUint8(0x01);
    input.moduleKey().encode(encoder);
    return true;
  }
  if (value.is<CallableLabelOwner>()) {
    encoder.encodeUint8(0x02);
    return encodeDefinition(encoder, input, value.get<CallableLabelOwner>().callable);
  }
  const auto& anonymous = value.get<AnonymousLabelOwner>();
  if (anonymous.module != input.module()) { return false; }
  encoder.encodeUint8(0x03);
  return encodeAnonymous(encoder, input, anonymous.anonymous);
}

bool encodeLabelId(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                   const LabelId& identity) {
  if (!identity.belongsTo(input.semanticContext()) ||
      !encodeLabelOwner(encoder, input, identity.owner())) {
    return false;
  }
  encoder.encodeUint32(identity.index());
  return true;
}

bool encodeLabelTarget(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                       const LabelTarget& target) {
  const auto& value = target.value();
  if (value.is<BlockLabelTarget>()) {
    encoder.encodeUint8(0x01);
    return encodeScopeId(encoder, input, value.get<BlockLabelTarget>().scope);
  }
  encoder.encodeUint8(0x02);
  return encodeScopeId(encoder, input, value.get<LoopLabelTarget>().scope);
}

bool encodeControlTarget(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                         const ControlTarget& target) {
  if (target.is<ExplicitLabelControlTarget>()) {
    encoder.encodeUint8(0x01);
    return encodeLabelId(encoder, input, target.get<ExplicitLabelControlTarget>().label);
  }
  if (target.is<LoopControlTarget>()) {
    encoder.encodeUint8(0x02);
    return encodeScopeId(encoder, input, target.get<LoopControlTarget>().scope);
  }
  if (target.is<MatchControlTarget>()) {
    encoder.encodeUint8(0x03);
    return encodeScopeId(encoder, input, target.get<MatchControlTarget>().scope);
  }
  return false;
}

bool encodeLabelFact(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                     const LabelFact& fact) {
  if (fact.identity.owner() != fact.owner || !encodeLabelId(encoder, input, fact.identity)) {
    return false;
  }
  fact.name.encode(encoder);
  if (!encodeLabelOwner(encoder, input, fact.owner)) { return false; }
  encoder.encodeUint32(fact.statement.value);
  if (!encodeLabelTarget(encoder, input, fact.target)) { return false; }
  fact.source.encode(encoder);
  return true;
}

bool encodeImplementation(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                          identity::ImplId implementation) {
  ZC_IF_SOME(key, input.definitions().implKey(implementation)) {
    key.encode(encoder);
    return true;
  }
  return false;
}

bool encodeImplOccurrence(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                          ImplOccurrenceId occurrence) {
  for (const auto& entry : input.definitions().impls()) {
    if (entry.occurrence == occurrence) {
      entry.key.encode(encoder);
      return true;
    }
  }
  return false;
}

bool encodeGenericParameter(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                            identity::GenericParameterId parameter) {
  ZC_IF_SOME(key, input.definitions().genericParameterKey(parameter)) {
    key.encode(encoder);
    return true;
  }
  return false;
}

bool encodeCallableParameter(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                             identity::CallableParameterId parameter) {
  ZC_IF_SOME(key, input.definitions().callableParameterKey(parameter)) {
    key.encode(encoder);
    return true;
  }
  return false;
}

bool encodeOwnerLocal(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      OwnerLocalBindingId binding) {
  for (const auto& entry : input.definitions().ownerLocalBindings()) {
    if (entry.binding == binding) {
      entry.key.encode(encoder);
      return true;
    }
  }
  return false;
}

bool encodeTarget(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                  const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    encoder.encodeUint8(0x01);
    return encodeDefinition(encoder, input, value.get<DefinitionBindingTarget>().definition);
  }
  if (value.is<GenericParameterBindingTarget>()) {
    encoder.encodeUint8(0x02);
    return encodeGenericParameter(encoder, input,
                                  value.get<GenericParameterBindingTarget>().parameter);
  }
  if (value.is<CallableParameterBindingTarget>()) {
    encoder.encodeUint8(0x03);
    return encodeCallableParameter(encoder, input,
                                   value.get<CallableParameterBindingTarget>().parameter);
  }
  if (value.is<OwnerLocalBindingTarget>()) {
    encoder.encodeUint8(0x04);
    return encodeOwnerLocal(encoder, input, value.get<OwnerLocalBindingTarget>().binding);
  }
  if (value.is<SemanticImportBindingTarget>()) {
    encoder.encodeUint8(0x05);
    encoder.encodeByteString(value.get<SemanticImportBindingTarget>().binding.encode().asPtr());
    return true;
  }
  const auto module = value.get<ModuleBindingTarget>().module;
  encoder.encodeUint8(0x06);
  ZC_IF_SOME(key, input.moduleKey(module)) {
    key.encode(encoder);
    return true;
  }
  return false;
}

void encodeName(identity::CanonicalEncoder& encoder, const BindingNameKey& name) {
  encoder.encodeUint8(static_cast<uint8_t>(name.nameSpace()));
  name.name().encode(encoder);
}

bool encodeDeferredMemberFact(identity::CanonicalEncoder& encoder,
                              const VerifiedBindingInput& input, const DeferredMemberFact& fact) {
  const auto& tree = input.tree();
  if (!tree.contains(fact.node) || !tree.contains(fact.base)) { return false; }
  encoder.encodeUint32(fact.node.value);
  encoder.encodeUint32(fact.base.value);
  fact.member.encode(encoder);
  encoder.encodeSequenceSize(fact.expectedNamespaces.size());
  for (const auto nameSpace : fact.expectedNamespaces) {
    encoder.encodeUint8(static_cast<uint8_t>(nameSpace));
  }
  encoder.encodeSequenceSize(fact.genericArguments.size());
  for (const auto argument : fact.genericArguments) {
    if (!tree.contains(argument)) { return false; }
    encoder.encodeUint32(argument.value);
  }
  fact.source.encode(encoder);
  return true;
}

bool encodeVisibility(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const VisibilityEnvelope& visibility) {
  const auto& value = visibility.value();
  if (value.is<ModuleVisibility>()) {
    if (value.get<ModuleVisibility>().module != input.module()) { return false; }
    encoder.encodeUint8(0x01);
    input.moduleKey().encode(encoder);
    return true;
  }
  encoder.encodeUint8(0x02);
  return true;
}

void encodeMaybeSpan(identity::CanonicalEncoder& encoder,
                     const zc::Maybe<identity::SourceSpan>& span) {
  ZC_IF_SOME(value, span) {
    encoder.encodeSome();
    value.encode(encoder);
    return;
  }
  encoder.encodeNone();
}

bool encodeReexportChain(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                         zc::ArrayPtr<const ReexportProvenanceStep> chain) {
  encoder.encodeSequenceSize(chain.size());
  for (const auto& step : chain) {
    ZC_IF_SOME(moduleKey, input.moduleKey(step.module)) {
      moduleKey.encode(encoder);
    } else {
      return false;
    }
    if (!encodeTarget(encoder, input, step.bindingIdentity) ||
        !encodeTarget(encoder, input, step.canonicalTarget)) {
      return false;
    }
    step.exportSpan.encode(encoder);
  }
  return true;
}

bool encodeEntry(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                 const ExportSurfaceEntry& entry) {
  encodeName(encoder, entry.name);
  if (!encodeTarget(encoder, input, entry.bindingIdentity) ||
      !encodeTarget(encoder, input, entry.canonicalTarget) ||
      !encodeVisibility(encoder, input, entry.visibility)) {
    return false;
  }
  encoder.encodeBool(entry.exported);
  entry.bindingSpan.encode(encoder);
  entry.canonicalDeclarationSpan.encode(encoder);
  encodeMaybeSpan(encoder, entry.aliasSpan);
  encodeMaybeSpan(encoder, entry.exportSpan);
  return encodeReexportChain(encoder, input, entry.reexportChain.asPtr());
}

zc::Maybe<zc::Array<uint8_t>> encodeSurfaceMap(const VerifiedBindingInput& input,
                                               zc::ArrayPtr<const ExportSurfaceEntry> entries) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(entries.size());
  for (const auto& entry : entries) {
    encodeName(encoder, entry.name);
    if (!encodeEntry(encoder, input, entry)) { return zc::none; }
  }
  return encoder.finish();
}

bool encodeScopeOwner(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ScopeOwner& owner) {
  const auto& value = owner.value();
  if (value.is<ModuleScopeOwner>()) {
    if (value.get<ModuleScopeOwner>().module != input.module()) { return false; }
    encoder.encodeUint8(0x01);
    input.moduleKey().encode(encoder);
    return true;
  }
  if (value.is<DefinitionScopeOwner>()) {
    encoder.encodeUint8(0x02);
    return encodeDefinition(encoder, input, value.get<DefinitionScopeOwner>().definition);
  }
  if (value.is<ImplScopeOwner>()) {
    encoder.encodeUint8(0x03);
    return encodeImplOccurrence(encoder, input, value.get<ImplScopeOwner>().occurrence);
  }
  encoder.encodeUint8(0x04);
  return encodeAnonymous(encoder, input, value.get<AnonymousScopeOwner>().anonymous);
}

zc::Maybe<zc::Array<uint8_t>> encodeAllocationScopeRecord(const VerifiedBindingInput& input,
                                                          const ScopeRecord& scope) {
  if (scope.id.module() != input.module() || !scope.id.belongsTo(input.semanticContext())) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  input.moduleKey().encode(encoder);
  encoder.encodeUint32(scope.id.index());
  ZC_IF_SOME(parent, scope.parent) {
    if (parent.module() != input.module() || !parent.belongsTo(input.semanticContext())) {
      return zc::none;
    }
    encoder.encodeSome();
    encoder.encodeUint32(parent.index());
  } else {
    encoder.encodeNone();
  }
  if (!encodeScopeOwner(encoder, input, scope.owner)) { return zc::none; }
  encoder.encodeUint8(static_cast<uint8_t>(scope.kind));
  scope.source.encode(encoder);
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeAllocationLabelRecord(const VerifiedBindingInput& input,
                                                          zc::ArrayPtr<const ScopeRecord> scopes,
                                                          const LabelFact& fact) {
  if (fact.identity.owner() != fact.owner || !fact.identity.belongsTo(input.semanticContext())) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  if (!encodeLabelOwner(encoder, input, fact.owner)) { return zc::none; }
  encoder.encodeUint32(fact.identity.index());
  fact.name.encode(encoder);
  const auto& target = fact.target.value();
  ScopeId scope = target.is<BlockLabelTarget>() ? target.get<BlockLabelTarget>().scope
                                                : target.get<LoopLabelTarget>().scope;
  encoder.encodeUint8(target.is<BlockLabelTarget>() ? 0x01 : 0x02);
  if (scope.module() != input.module() || !scope.belongsTo(input.semanticContext()) ||
      scope.index() >= scopes.size() || scopes[scope.index()].id != scope) {
    return zc::none;
  }
  const auto expectedKind = target.is<BlockLabelTarget>() ? ScopeKind::Block : ScopeKind::Loop;
  if (scopes[scope.index()].kind != expectedKind) { return zc::none; }
  encoder.encodeUint32(scope.index());
  fact.source.encode(encoder);
  return encoder.finish();
}

bool encodeNameBinding(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                       const NameBinding& binding) {
  if (!encodeTarget(encoder, input, binding.bindingIdentity) ||
      !encodeTarget(encoder, input, binding.canonicalTarget)) {
    return false;
  }
  encoder.encodeUint8(static_cast<uint8_t>(binding.nameSpace));
  encoder.encodeUint8(static_cast<uint8_t>(binding.origin));
  binding.declarationSpan.encode(encoder);
  encodeMaybeSpan(encoder, binding.aliasSpan);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput&,
                      const BindingFailureRef& fact) {
  encoder.encodeUint32(static_cast<uint32_t>(fact.diagnostic));
  fact.primary.encode(encoder);
  encoder.encodeUint64(fact.emitterOrdinal);
  encoder.encodeSequenceSize(fact.notes.size());
  for (const auto& note : fact.notes) {
    encoder.encodeUint32(static_cast<uint32_t>(note.diagnostic));
    note.source.encode(encoder);
  }
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const NodeScopeFact& fact) {
  encoder.encodeUint32(fact.node.value);
  return encodeScopeId(encoder, input, fact.scope);
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const BindingResolution& fact) {
  encoder.encodeUint32(fact.node.value);
  const auto& value = fact.value;
  if (value.is<BoundNameResolution>()) {
    const auto& bound = value.get<BoundNameResolution>();
    encoder.encodeUint8(0x01);
    if (!encodeTarget(encoder, input, bound.bindingIdentity) ||
        !encodeTarget(encoder, input, bound.canonicalTarget)) {
      return false;
    }
    encoder.encodeUint8(static_cast<uint8_t>(bound.nameSpace));
    encoder.encodeUint8(static_cast<uint8_t>(bound.origin));
    return true;
  }
  if (value.is<BoundLabelResolution>()) {
    const auto& bound = value.get<BoundLabelResolution>();
    encoder.encodeUint8(0x02);
    return encodeLabelId(encoder, input, bound.label) &&
           encodeLabelTarget(encoder, input, bound.target);
  }
  if (value.is<DeferredMemberFact>()) {
    encoder.encodeUint8(0x03);
    return encodeDeferredMemberFact(encoder, input, value.get<DeferredMemberFact>());
  }
  if (!value.is<FailedBindingResolution>()) { return false; }
  encoder.encodeUint8(0x04);
  encoder.encodeUint64(value.get<FailedBindingResolution>().failureIndex);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const BoundSelfType& fact) {
  if (!input.tree().contains(fact.syntax)) { return false; }
  encoder.encodeUint32(fact.syntax.value);
  if (fact.owner.is<NominalSelfOwner>()) {
    encoder.encodeUint8(0x01);
    if (!encodeDefinition(encoder, input, fact.owner.get<NominalSelfOwner>().definition)) {
      return false;
    }
  } else if (fact.owner.is<InterfaceSelfOwner>()) {
    encoder.encodeUint8(0x02);
    if (!encodeDefinition(encoder, input, fact.owner.get<InterfaceSelfOwner>().definition)) {
      return false;
    }
  } else if (fact.owner.is<ImplSelfOwner>()) {
    encoder.encodeUint8(0x03);
    if (!encodeImplOccurrence(encoder, input, fact.owner.get<ImplSelfOwner>().occurrence)) {
      return false;
    }
  } else {
    return false;
  }
  fact.source.encode(encoder);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const BoundThis& fact) {
  if (!input.tree().contains(fact.expression)) { return false; }
  encoder.encodeUint32(fact.expression.value);
  if (!encodeCallableParameter(encoder, input, fact.binding.receiverParameter)) { return false; }
  fact.source.encode(encoder);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const DefinitionFact& fact) {
  if (!encodeDefinition(encoder, input, fact.identity)) { return false; }
  const auto& site = fact.site.value();
  if (site.is<DeclarationDefinitionSite>()) {
    encoder.encodeUint8(0x01);
    encoder.encodeUint32(site.get<DeclarationDefinitionSite>().node.value);
  } else {
    const auto& pattern = site.get<PatternBindingSite>();
    encoder.encodeUint8(0x02);
    encoder.encodeUint32(pattern.introducer.value);
    encoder.encodeSequenceSize(pattern.patternPath.size());
    for (const auto component : pattern.patternPath) { encoder.encodeUint32(component); }
  }
  encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
  fact.name.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(fact.nameSpace));
  if (!encodeScopeId(encoder, input, fact.declaringScope)) { return false; }
  fact.source.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(fact.activation));
  encoder.encodeBool(fact.memberVisibility != zc::none);
  ZC_IF_SOME(visibility, fact.memberVisibility) {
    encoder.encodeUint8(static_cast<uint8_t>(visibility));
  }
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ImplBindingFact& fact) {
  if (!encodeImplOccurrence(encoder, input, fact.occurrence) ||
      !encodeImplementation(encoder, input, fact.authority)) {
    return false;
  }
  encoder.encodeUint32(fact.node.value);
  if (!encodeScopeId(encoder, input, fact.scope)) { return false; }
  encoder.encodeSequenceSize(fact.members.size());
  for (const auto member : fact.members) {
    if (!encodeDefinition(encoder, input, member)) { return false; }
  }
  fact.source.encode(encoder);
  return true;
}

bool encodeDefinitionSite(identity::CanonicalEncoder& encoder, const DefinitionSite& site) {
  const auto& value = site.value();
  if (value.is<DeclarationDefinitionSite>()) {
    encoder.encodeUint8(0x01);
    encoder.encodeUint32(value.get<DeclarationDefinitionSite>().node.value);
    return true;
  }
  if (!value.is<PatternBindingSite>()) { return false; }
  const auto& pattern = value.get<PatternBindingSite>();
  encoder.encodeUint8(0x02);
  encoder.encodeUint32(pattern.introducer.value);
  encoder.encodeSequenceSize(pattern.patternPath.size());
  for (const auto component : pattern.patternPath) { encoder.encodeUint32(component); }
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const GenericParameterFact& fact) {
  if (!encodeGenericParameter(encoder, input, fact.identity) ||
      !encodeDefinitionSite(encoder, fact.site)) {
    return false;
  }
  fact.name.encode(encoder);
  if (!encodeScopeId(encoder, input, fact.declaringScope)) { return false; }
  fact.source.encode(encoder);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const CallableParameterFact& fact) {
  if (!encodeCallableParameter(encoder, input, fact.identity) ||
      !encodeDefinitionSite(encoder, fact.site)) {
    return false;
  }
  ZC_IF_SOME(name, fact.name) {
    encoder.encodeSome();
    name.encode(encoder);
  } else {
    encoder.encodeNone();
  }
  if (!encodeScopeId(encoder, input, fact.declaringScope)) { return false; }
  fact.source.encode(encoder);
  encoder.encodeBool(fact.receiver);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const OwnerLocalBindingFact& fact) {
  if (!encodeOwnerLocal(encoder, input, fact.identity) ||
      !encodeDefinitionSite(encoder, fact.site)) {
    return false;
  }
  encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
  fact.name.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(fact.nameSpace));
  if (!encodeScopeId(encoder, input, fact.declaringScope)) { return false; }
  fact.source.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(fact.activation));
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ScopeRecord& fact) {
  if (!encodeScopeId(encoder, input, fact.id)) { return false; }
  ZC_IF_SOME(parent, fact.parent) {
    encoder.encodeSome();
    if (!encodeScopeId(encoder, input, parent)) { return false; }
  } else {
    encoder.encodeNone();
  }
  if (!encodeScopeOwner(encoder, input, fact.owner)) { return false; }
  encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
  encoder.encodeSequenceSize(fact.bindings.size());
  for (const auto& binding : fact.bindings) {
    encodeName(encoder, binding.name);
    if (!encodeNameBinding(encoder, input, binding.binding)) { return false; }
  }
  fact.source.encode(encoder);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ModuleAliasBindingFact& fact) {
  encoder.encodeUint32(fact.node.value);
  if (!encodeDefinition(encoder, input, fact.alias)) { return false; }
  ZC_IF_SOME(moduleKey, input.moduleKey(fact.canonicalTarget)) {
    moduleKey.encode(encoder);
  } else {
    return false;
  }
  encoder.encodeDigest(fact.targetRevision.digest());
  fact.declarationSpan.encode(encoder);
  fact.targetSpan.encode(encoder);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ImportBindingFact& fact) {
  encoder.encodeUint32(fact.node.value);
  encoder.encodeByteString(fact.binding.encode().asPtr());
  if (!encodeTarget(encoder, input, fact.canonicalTarget)) { return false; }
  ZC_IF_SOME(moduleKey, input.moduleKey(fact.sourceModule)) {
    moduleKey.encode(encoder);
  } else {
    return false;
  }
  encoder.encodeDigest(fact.sourceRevision.digest());
  encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
  fact.declarationSpan.encode(encoder);
  encodeMaybeSpan(encoder, fact.aliasSpan);
  return encodeReexportChain(encoder, input, fact.reexportChain.asPtr());
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const LocalExportFact& fact) {
  encoder.encodeUint32(fact.node.value);
  if (!encodeTarget(encoder, input, fact.sourceBinding) ||
      !encodeTarget(encoder, input, fact.canonicalTarget)) {
    return false;
  }
  fact.bindingSpan.encode(encoder);
  fact.canonicalDeclarationSpan.encode(encoder);
  encodeMaybeSpan(encoder, fact.aliasSpan);
  fact.exportSpan.encode(encoder);
  return encodeReexportChain(encoder, input, fact.reexportChain.asPtr());
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const DeferredMemberFact& fact) {
  return encodeDeferredMemberFact(encoder, input, fact);
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const LabelFact& fact) {
  return encodeLabelFact(encoder, input, fact);
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ControlTransferFact& fact) {
  encoder.encodeUint32(fact.node.value);
  encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
  if (!encodeControlTarget(encoder, input, fact.target)) { return false; }
  fact.source.encode(encoder);
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ShadowTargetFact& fact) {
  return encodeTarget(encoder, input, fact.binding) && encodeTarget(encoder, input, fact.target);
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ClosureFreeVariableFact& fact) {
  if (!encodeAnonymous(encoder, input, fact.closure)) { return false; }
  encoder.encodeSequenceSize(fact.variables.size());
  for (const auto& variable : fact.variables) {
    if (!encodeTarget(encoder, input, variable.target)) { return false; }
    encoder.encodeSequenceSize(variable.referenceSites.size());
    for (const auto site : variable.referenceSites) {
      if (!input.tree().contains(site)) { return false; }
      encoder.encodeUint32(site.value);
    }
  }
  return true;
}

bool encodeFactRecord(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ExplicitClosureCaptureFact& fact) {
  if (!encodeAnonymous(encoder, input, fact.closure)) { return false; }
  if (!input.tree().contains(fact.captureList)) { return false; }
  encoder.encodeUint32(fact.captureList.value);
  fact.source.encode(encoder);
  encoder.encodeSequenceSize(fact.captures.size());
  for (const auto& capture : fact.captures) {
    if (!input.tree().contains(capture.item) || !encodeTarget(encoder, input, capture.target)) {
      return false;
    }
    encoder.encodeUint32(capture.item.value);
    capture.source.encode(encoder);
  }
  return true;
}

template <typename Fact>
bool encodeFactSequence(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                        zc::ArrayPtr<const Fact> facts, uint8_t stableTag) {
  encoder.encodeUint8(stableTag);
  encoder.encodeSequenceSize(facts.size());
  for (const auto& fact : facts) {
    if (!encodeFactRecord(encoder, input, fact)) { return false; }
  }
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeCandidate(const VerifiedBindingInput& input,
                                              const BindingMetadataCandidate& candidate) {
  identity::CanonicalEncoder encoder;
  input.moduleKey().encode(encoder);
#define ZOM_BINDING_FACT(id, type, member, accessor, publication, tag, domain, mutations, test) \
  if (!encodeFactSequence(encoder, input, candidate.member.asPtr(), tag)) { return zc::none; }
#include "zomlang/compiler/binder/binding-fact-schema.def"
#undef ZOM_BINDING_FACT
  if (candidate.currentSurface.sourceModule != input.module() ||
      candidate.currentSurface.sourceCompilationUnit != input.compilationUnit()) {
    return zc::none;
  }
  input.moduleKey().encode(encoder);
  input.compilationUnitKey().encode(encoder);
  encoder.encodeDigest(candidate.currentSurface.revision.digest());
  ZC_IF_SOME(visible, encodeSurfaceMap(input, candidate.currentSurface.visibleEntries.asPtr())) {
    encoder.encodeByteString(visible.asPtr());
  } else {
    return zc::none;
  }
  ZC_IF_SOME(exports, encodeSurfaceMap(input, candidate.currentSurface.exports.asPtr())) {
    encoder.encodeByteString(exports.asPtr());
  } else {
    return zc::none;
  }
  return encoder.finish();
}

}  // namespace

zc::Maybe<zc::Array<uint8_t>> encodeBindingCandidate(const VerifiedBindingInput& input,
                                                     const BindingMetadataCandidate& candidate) {
  return encodeCandidate(input, candidate);
}

zc::Maybe<zc::Array<uint8_t>> encodeBindingSurfaceMap(
    const VerifiedBindingInput& input, zc::ArrayPtr<const ExportSurfaceEntry> entries) {
  return encodeSurfaceMap(input, entries);
}

zc::Maybe<zc::Array<uint8_t>> encodeBindingSurfaceEntry(const VerifiedBindingInput& input,
                                                        const ExportSurfaceEntry& entry) {
  identity::CanonicalEncoder encoder;
  if (!encodeEntry(encoder, input, entry)) { return zc::none; }
  return encoder.finish();
}

bool encodeBindingExtensionSequences(identity::CanonicalEncoder& encoder,
                                     const VerifiedBindingInput& input,
                                     zc::ArrayPtr<const BoundSelfType> selfTypes,
                                     zc::ArrayPtr<const BoundThis> thisBindings) {
  encoder.encodeSequenceSize(selfTypes.size());
  for (const auto& fact : selfTypes) {
    if (!input.tree().contains(fact.syntax)) { return false; }
    encoder.encodeUint32(fact.syntax.value);
    if (fact.owner.is<NominalSelfOwner>()) {
      encoder.encodeUint8(0x01);
      if (!encodeDefinition(encoder, input, fact.owner.get<NominalSelfOwner>().definition)) {
        return false;
      }
    } else if (fact.owner.is<InterfaceSelfOwner>()) {
      encoder.encodeUint8(0x02);
      if (!encodeDefinition(encoder, input, fact.owner.get<InterfaceSelfOwner>().definition)) {
        return false;
      }
    } else if (fact.owner.is<ImplSelfOwner>()) {
      encoder.encodeUint8(0x03);
      if (!encodeImplOccurrence(encoder, input, fact.owner.get<ImplSelfOwner>().occurrence)) {
        return false;
      }
    } else {
      return false;
    }
    fact.source.encode(encoder);
  }
  encoder.encodeSequenceSize(thisBindings.size());
  for (const auto& fact : thisBindings) {
    if (!input.tree().contains(fact.expression)) { return false; }
    encoder.encodeUint32(fact.expression.value);
    encoder.encodeUint8(0x01);
    if (!encodeCallableParameter(encoder, input, fact.binding.receiverParameter)) { return false; }
    fact.source.encode(encoder);
  }
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeBindingAllocationDump(const VerifiedBindingInput& input,
                                                          zc::ArrayPtr<const ScopeRecord> scopes,
                                                          zc::ArrayPtr<const LabelFact> labels) {
  zc::Vector<zc::Array<uint8_t>> scopeStorage;
  for (size_t index = 0; index < scopes.size(); ++index) {
    if (scopes[index].id.index() != index) { return zc::none; }
    auto encoded = encodeAllocationScopeRecord(input, scopes[index]);
    ZC_IF_SOME(value, encoded) {
      scopeStorage.add(zc::mv(value));
    } else {
      return zc::none;
    }
  }
  zc::Vector<zc::Array<uint8_t>> labelStorage;
  zc::Array<uint8_t> previousLabelIdentity;
  bool hasPreviousLabelIdentity = false;
  for (const auto& fact : labels) {
    if (fact.identity.owner() != fact.owner) { return zc::none; }
    identity::CanonicalEncoder identityEncoder;
    if (!encodeLabelId(identityEncoder, input, fact.identity)) { return zc::none; }
    auto labelIdentity = identityEncoder.finish();
    if (hasPreviousLabelIdentity &&
        compareCanonicalBytes(previousLabelIdentity.asPtr(), labelIdentity.asPtr()) >= 0) {
      return zc::none;
    }
    previousLabelIdentity = zc::mv(labelIdentity);
    hasPreviousLabelIdentity = true;
    auto encoded = encodeAllocationLabelRecord(input, scopes, fact);
    ZC_IF_SOME(value, encoded) {
      labelStorage.add(zc::mv(value));
    } else {
      return zc::none;
    }
  }
  zc::Vector<zc::ArrayPtr<const uint8_t>> scopeRecords;
  for (const auto& value : scopeStorage) { scopeRecords.add(value.asPtr()); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> labelRecords;
  for (const auto& value : labelStorage) { labelRecords.add(value.asPtr()); }
  return frameBindingAllocationDump(scopeRecords.asPtr(), labelRecords.asPtr());
}

}  // namespace zomlang::compiler::binder
