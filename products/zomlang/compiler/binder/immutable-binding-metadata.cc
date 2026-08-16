// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/immutable-binding-metadata.h"

namespace zomlang::compiler::binder {
namespace {

bool sameOwner(const StableOwnerBodyQueryKey& left, const StableOwnerBodyQueryKey& right) {
  return left.encodeCanonical().asPtr() == right.encodeCanonical().asPtr();
}

bool exactOwnerCoverage(const BoundModuleSkeleton& skeleton,
                        zc::ArrayPtr<const BoundOwnerBody> ownerBodies) {
  const auto expected = skeleton.bodyOwners().values();
  if (expected.size() != ownerBodies.size()) { return false; }
  for (size_t index = 0; index < expected.size(); ++index) {
    if (!sameOwner(expected[index], ownerBodies[index].owner())) { return false; }
  }
  return true;
}

zc::Vector<NodeScopeFact> cloneNodeScopes(zc::ArrayPtr<const NodeScopeFact> facts) {
  zc::Vector<NodeScopeFact> result(facts.size());
  for (const auto& fact : facts) { result.add(NodeScopeFact{fact.node, fact.scope}); }
  return result;
}

zc::Vector<DefinitionFact> cloneDefinitions(zc::ArrayPtr<const DefinitionFact> facts) {
  zc::Vector<DefinitionFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Maybe<MemberVisibility> visibility;
    ZC_IF_SOME(value, fact.memberVisibility) { visibility = value; }
    result.add(DefinitionFact(fact.identity, fact.site.clone(), fact.kind, fact.name.clone(),
                              fact.nameSpace, fact.declaringScope, fact.source.clone(),
                              fact.activation, zc::mv(visibility)));
  }
  return result;
}

zc::Vector<GenericParameterFact> cloneGenericParameters(
    zc::ArrayPtr<const GenericParameterFact> facts) {
  zc::Vector<GenericParameterFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(GenericParameterFact{fact.identity, fact.site.clone(), fact.name.clone(),
                                    fact.declaringScope, fact.source.clone()});
  }
  return result;
}

zc::Vector<CallableParameterFact> cloneCallableParameters(
    zc::ArrayPtr<const CallableParameterFact> facts) {
  zc::Vector<CallableParameterFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Maybe<identity::DeclaredDefinitionName> name;
    ZC_IF_SOME(value, fact.name) { name = value.clone(); }
    result.add(CallableParameterFact{fact.identity, fact.site.clone(), zc::mv(name),
                                     fact.declaringScope, fact.source.clone(), fact.receiver});
  }
  return result;
}

zc::Vector<OwnerLocalBindingFact> cloneOwnerLocalBindings(
    zc::ArrayPtr<const OwnerLocalBindingFact> facts) {
  zc::Vector<OwnerLocalBindingFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(OwnerLocalBindingFact{fact.identity, fact.node, fact.site.clone(), fact.kind,
                                     fact.name.clone(), fact.nameSpace, fact.declaringScope,
                                     fact.source.clone(), fact.activation});
  }
  return result;
}

zc::Vector<ImplBindingFact> cloneImplementations(zc::ArrayPtr<const ImplBindingFact> facts) {
  zc::Vector<ImplBindingFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Vector<identity::DefId> members(fact.members.size());
    for (const auto member : fact.members) { members.add(member); }
    result.add(ImplBindingFact{fact.occurrence, fact.authority, fact.node, fact.scope,
                               zc::mv(members), fact.source.clone()});
  }
  return result;
}

zc::Vector<ScopeRecord> cloneScopes(zc::ArrayPtr<const ScopeRecord> records) {
  zc::Vector<ScopeRecord> result(records.size());
  for (const auto& record : records) {
    zc::Maybe<ScopeId> parent;
    ZC_IF_SOME(value, record.parent) { parent = value; }
    zc::Vector<ScopeBindingEntry> bindings(record.bindings.size());
    for (const auto& entry : record.bindings) {
      zc::Maybe<identity::SourceSpan> alias;
      ZC_IF_SOME(value, entry.binding.aliasSpan) { alias = value.clone(); }
      bindings.add(ScopeBindingEntry(
          entry.name.clone(),
          NameBinding(entry.binding.bindingIdentity.clone(), entry.binding.canonicalTarget.clone(),
                      entry.binding.nameSpace, entry.binding.origin,
                      entry.binding.declarationSpan.clone(), zc::mv(alias))));
    }
    result.add(ScopeRecord(record.id, zc::mv(parent), record.owner.clone(), record.kind,
                           zc::mv(bindings), record.source.clone()));
  }
  return result;
}

zc::Vector<ModuleAliasBindingFact> cloneModuleAliases(
    zc::ArrayPtr<const ModuleAliasBindingFact> facts) {
  zc::Vector<ModuleAliasBindingFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(ModuleAliasBindingFact{
        fact.node, fact.alias, fact.canonicalTarget,
        ModuleAliasExportNamesRevision::fromDigest(fact.targetExportNamesRevision.digest()),
        fact.declarationSpan.clone(), fact.targetSpan.clone()});
  }
  return result;
}

zc::Vector<ImportBindingFact> cloneImports(zc::ArrayPtr<const ImportBindingFact> facts) {
  zc::Vector<ImportBindingFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Maybe<identity::SourceSpan> alias;
    ZC_IF_SOME(value, fact.aliasSpan) { alias = value.clone(); }
    zc::Vector<ReexportProvenanceStep> chain(fact.reexportChain.size());
    for (const auto& step : fact.reexportChain) { chain.add(step.clone()); }
    result.add(ImportBindingFact{
        fact.node, fact.binding.clone(), fact.canonicalTarget.clone(), fact.sourceModule,
        ExportSurfaceRevision::fromDigest(fact.sourceRevision.digest()), fact.kind,
        fact.declarationSpan.clone(), zc::mv(alias), zc::mv(chain)});
  }
  return result;
}

zc::Vector<LocalExportFact> cloneLocalExports(zc::ArrayPtr<const LocalExportFact> facts) {
  zc::Vector<LocalExportFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Maybe<identity::SourceSpan> alias;
    ZC_IF_SOME(value, fact.aliasSpan) { alias = value.clone(); }
    zc::Vector<ReexportProvenanceStep> chain(fact.reexportChain.size());
    for (const auto& step : fact.reexportChain) { chain.add(step.clone()); }
    result.add(LocalExportFact{fact.node, fact.sourceBinding.clone(), fact.canonicalTarget.clone(),
                               fact.bindingSpan.clone(), fact.canonicalDeclarationSpan.clone(),
                               zc::mv(alias), fact.exportSpan.clone(), zc::mv(chain)});
  }
  return result;
}

zc::Vector<DeferredMemberFact> cloneDeferredMembers(zc::ArrayPtr<const DeferredMemberFact> facts) {
  zc::Vector<DeferredMemberFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Vector<Namespace> namespaces(fact.expectedNamespaces.size());
    for (const auto value : fact.expectedNamespaces) { namespaces.add(value); }
    zc::Vector<ast::NodeId> arguments(fact.genericArguments.size());
    for (const auto value : fact.genericArguments) { arguments.add(value); }
    result.add(DeferredMemberFact{fact.node, fact.base, fact.member.clone(), zc::mv(namespaces),
                                  zc::mv(arguments), fact.source.clone()});
  }
  return result;
}

zc::Vector<BindingResolution> cloneNodeBindings(zc::ArrayPtr<const BindingResolution> facts) {
  zc::Vector<BindingResolution> result(facts.size());
  for (const auto& fact : facts) {
    const auto& value = fact.value;
    if (value.is<BoundNameResolution>()) {
      const auto& name = value.get<BoundNameResolution>();
      result.add(BindingResolution{
          fact.node, BindingResolutionValue(BoundNameResolution{name.bindingIdentity.clone(),
                                                                name.canonicalTarget.clone(),
                                                                name.nameSpace, name.origin})});
      continue;
    }
    if (value.is<BoundLabelResolution>()) {
      const auto& label = value.get<BoundLabelResolution>();
      result.add(BindingResolution{fact.node, BindingResolutionValue(BoundLabelResolution{
                                                  label.label.clone(), label.target.clone()})});
      continue;
    }
    if (value.is<DeferredMemberFact>()) {
      const auto& member = value.get<DeferredMemberFact>();
      zc::Vector<Namespace> namespaces(member.expectedNamespaces.size());
      for (const auto nameSpace : member.expectedNamespaces) { namespaces.add(nameSpace); }
      zc::Vector<ast::NodeId> arguments(member.genericArguments.size());
      for (const auto argument : member.genericArguments) { arguments.add(argument); }
      result.add(BindingResolution{
          fact.node, BindingResolutionValue(DeferredMemberFact{
                         member.node, member.base, member.member.clone(), zc::mv(namespaces),
                         zc::mv(arguments), member.source.clone()})});
      continue;
    }
    result.add(
        BindingResolution{fact.node, BindingResolutionValue(FailedBindingResolution{
                                         value.get<FailedBindingResolution>().failureIndex})});
  }
  return result;
}

zc::Vector<BoundSelfType> cloneSelfTypes(zc::ArrayPtr<const BoundSelfType> facts) {
  zc::Vector<BoundSelfType> result(facts.size());
  for (const auto& fact : facts) {
    if (fact.owner.is<NominalSelfOwner>()) {
      result.add(BoundSelfType{
          fact.syntax, SelfOwner(NominalSelfOwner{fact.owner.get<NominalSelfOwner>().definition}),
          fact.source.clone()});
      continue;
    }
    if (fact.owner.is<InterfaceSelfOwner>()) {
      result.add(BoundSelfType{
          fact.syntax,
          SelfOwner(InterfaceSelfOwner{fact.owner.get<InterfaceSelfOwner>().definition}),
          fact.source.clone()});
      continue;
    }
    result.add(BoundSelfType{fact.syntax,
                             SelfOwner(ImplSelfOwner{fact.owner.get<ImplSelfOwner>().occurrence}),
                             fact.source.clone()});
  }
  return result;
}

zc::Vector<BoundThis> cloneThisBindings(zc::ArrayPtr<const BoundThis> facts) {
  zc::Vector<BoundThis> result(facts.size());
  for (const auto& fact : facts) {
    result.add(BoundThis{fact.expression, ThisBinding{fact.binding.receiverParameter},
                         fact.source.clone()});
  }
  return result;
}

zc::Vector<LabelFact> cloneLabels(zc::ArrayPtr<const LabelFact> facts) {
  zc::Vector<LabelFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(LabelFact{fact.identity.clone(), fact.name.clone(), fact.owner.clone(),
                         fact.statement, fact.target.clone(), fact.source.clone()});
  }
  return result;
}

zc::Vector<ControlTransferFact> cloneControlTransfers(
    zc::ArrayPtr<const ControlTransferFact> facts) {
  zc::Vector<ControlTransferFact> result(facts.size());
  for (const auto& fact : facts) {
    const auto& target = fact.target;
    if (target.is<ExplicitLabelControlTarget>()) {
      result.add(ControlTransferFact{fact.node, fact.kind,
                                     ControlTarget(ExplicitLabelControlTarget{
                                         target.get<ExplicitLabelControlTarget>().label.clone()}),
                                     fact.source.clone()});
      continue;
    }
    if (target.is<LoopControlTarget>()) {
      result.add(ControlTransferFact{
          fact.node, fact.kind,
          ControlTarget(LoopControlTarget{target.get<LoopControlTarget>().scope}),
          fact.source.clone()});
      continue;
    }
    result.add(ControlTransferFact{
        fact.node, fact.kind,
        ControlTarget(MatchControlTarget{target.get<MatchControlTarget>().scope}),
        fact.source.clone()});
  }
  return result;
}

zc::Vector<ShadowTargetFact> cloneShadowTargets(zc::ArrayPtr<const ShadowTargetFact> facts) {
  zc::Vector<ShadowTargetFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(ShadowTargetFact{fact.binding.clone(), fact.target.clone()});
  }
  return result;
}

zc::Vector<ClosureFreeVariableFact> cloneClosureFreeVariables(
    zc::ArrayPtr<const ClosureFreeVariableFact> facts) {
  zc::Vector<ClosureFreeVariableFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Vector<FreeVariableFact> variables(fact.variables.size());
    for (const auto& variable : fact.variables) {
      zc::Vector<ast::NodeId> sites(variable.referenceSites.size());
      for (const auto site : variable.referenceSites) { sites.add(site); }
      variables.add(FreeVariableFact{variable.target.clone(), zc::mv(sites)});
    }
    result.add(ClosureFreeVariableFact{fact.closure.clone(), zc::mv(variables)});
  }
  return result;
}

zc::Vector<ExplicitClosureCaptureFact> cloneExplicitClosureCaptures(
    zc::ArrayPtr<const ExplicitClosureCaptureFact> facts) {
  zc::Vector<ExplicitClosureCaptureFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Vector<ExplicitCaptureBindingFact> captures(fact.captures.size());
    for (const auto& capture : fact.captures) {
      captures.add(
          ExplicitCaptureBindingFact{capture.item, capture.target.clone(), capture.source.clone()});
    }
    result.add(ExplicitClosureCaptureFact{fact.closure.clone(), fact.captureList,
                                          fact.source.clone(), zc::mv(captures)});
  }
  return result;
}

zc::Vector<MaterializedFailedLookupFact> cloneFailedLookups(
    zc::ArrayPtr<const MaterializedFailedLookupFact> facts) {
  zc::Vector<MaterializedFailedLookupFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(MaterializedFailedLookupFact{fact.node, fact.nameSpace, fact.name.clone(),
                                            fact.outcome.clone()});
  }
  return result;
}

}  // namespace

struct ImmutableBindingMetadata::Impl final {
  Impl(identity::SemanticContextBrand context, query::DatabaseRevision revision,
       identity::ContextFingerprint&& fingerprint, BoundModuleSkeleton&& skeleton,
       zc::Vector<BoundOwnerBody>&& ownerBodies, ModuleBindingAllocationPlan&& allocationPlan,
       zc::Vector<NodeScopeFact>&& nodeScopes, zc::Vector<BindingResolution>&& nodeBindings,
       zc::Vector<BoundSelfType>&& selfTypes, zc::Vector<BoundThis>&& thisBindings,
       zc::Vector<DefinitionFact>&& definitions, zc::Vector<ImplBindingFact>&& implementations,
       zc::Vector<ScopeRecord>&& scopes, zc::Vector<ModuleAliasBindingFact>&& moduleAliases,
       zc::Vector<ImportBindingFact>&& imports, zc::Vector<LocalExportFact>&& localExports,
       zc::Vector<DeferredMemberFact>&& deferredMembers, zc::Vector<LabelFact>&& labels,
       zc::Vector<ControlTransferFact>&& controlTransfers,
       zc::Vector<ShadowTargetFact>&& shadowTargets,
       zc::Vector<ClosureFreeVariableFact>&& closureFreeVariables,
       zc::Vector<ExplicitClosureCaptureFact>&& explicitClosureCaptures,
       zc::Vector<GenericParameterFact>&& genericParameters,
       zc::Vector<CallableParameterFact>&& callableParameters,
       zc::Vector<OwnerLocalBindingFact>&& ownerLocalBindings,
       zc::Vector<MaterializedFailedLookupFact>&& failedLookups)
      : context(context),
        revision(revision),
        fingerprint(zc::mv(fingerprint)),
        skeleton(zc::mv(skeleton)),
        ownerBodies(zc::mv(ownerBodies)),
        allocationPlan(zc::mv(allocationPlan)),
        nodeScopes(zc::mv(nodeScopes)),
        nodeBindings(zc::mv(nodeBindings)),
        selfTypes(zc::mv(selfTypes)),
        thisBindings(zc::mv(thisBindings)),
        definitions(zc::mv(definitions)),
        implementations(zc::mv(implementations)),
        scopes(zc::mv(scopes)),
        moduleAliases(zc::mv(moduleAliases)),
        imports(zc::mv(imports)),
        localExports(zc::mv(localExports)),
        deferredMembers(zc::mv(deferredMembers)),
        labels(zc::mv(labels)),
        controlTransfers(zc::mv(controlTransfers)),
        shadowTargets(zc::mv(shadowTargets)),
        closureFreeVariables(zc::mv(closureFreeVariables)),
        explicitClosureCaptures(zc::mv(explicitClosureCaptures)),
        genericParameters(zc::mv(genericParameters)),
        callableParameters(zc::mv(callableParameters)),
        ownerLocalBindings(zc::mv(ownerLocalBindings)),
        failedLookups(zc::mv(failedLookups)) {}

  identity::SemanticContextBrand context;
  query::DatabaseRevision revision;
  identity::ContextFingerprint fingerprint;
  BoundModuleSkeleton skeleton;
  zc::Vector<BoundOwnerBody> ownerBodies;
  ModuleBindingAllocationPlan allocationPlan;
  zc::Vector<NodeScopeFact> nodeScopes;
  zc::Vector<BindingResolution> nodeBindings;
  zc::Vector<BoundSelfType> selfTypes;
  zc::Vector<BoundThis> thisBindings;
  zc::Vector<DefinitionFact> definitions;
  zc::Vector<ImplBindingFact> implementations;
  zc::Vector<ScopeRecord> scopes;
  zc::Vector<ModuleAliasBindingFact> moduleAliases;
  zc::Vector<ImportBindingFact> imports;
  zc::Vector<LocalExportFact> localExports;
  zc::Vector<DeferredMemberFact> deferredMembers;
  zc::Vector<LabelFact> labels;
  zc::Vector<ControlTransferFact> controlTransfers;
  zc::Vector<ShadowTargetFact> shadowTargets;
  zc::Vector<ClosureFreeVariableFact> closureFreeVariables;
  zc::Vector<ExplicitClosureCaptureFact> explicitClosureCaptures;
  zc::Vector<GenericParameterFact> genericParameters;
  zc::Vector<CallableParameterFact> callableParameters;
  zc::Vector<OwnerLocalBindingFact> ownerLocalBindings;
  zc::Vector<MaterializedFailedLookupFact> failedLookups;
};

ImmutableBindingMetadata::ImmutableBindingMetadata(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
ImmutableBindingMetadata::~ImmutableBindingMetadata() noexcept(false) = default;
ImmutableBindingMetadata::ImmutableBindingMetadata(ImmutableBindingMetadata&&) noexcept = default;
ImmutableBindingMetadata& ImmutableBindingMetadata::operator=(ImmutableBindingMetadata&&) noexcept =
    default;

zc::Maybe<ImmutableBindingMetadata> ImmutableBindingMetadata::from(
    identity::SemanticContextBrand context, query::DatabaseRevision revision,
    const identity::ContextFingerprint& fingerprint, BoundModuleSkeleton&& skeleton,
    zc::Vector<BoundOwnerBody>&& ownerBodies, const MaterializedBindingFacts& facts) {
  if (!context.isValid() || revision.value() == 0 ||
      !exactOwnerCoverage(skeleton, ownerBodies.asPtr())) {
    return zc::none;
  }
  auto allocationPlan =
      ModuleBindingAllocationPlanner::from(skeleton, ownerBodies.asPtr().asConst());
  if (allocationPlan == zc::none) { return zc::none; }
  return ImmutableBindingMetadata(zc::heap<Impl>(
      context, revision, fingerprint.clone(), zc::mv(skeleton), zc::mv(ownerBodies),
      zc::mv(ZC_ASSERT_NONNULL(allocationPlan)), cloneNodeScopes(facts.nodeScopes),
      cloneNodeBindings(facts.nodeBindings), cloneSelfTypes(facts.selfTypes),
      cloneThisBindings(facts.thisBindings), cloneDefinitions(facts.definitions),
      cloneImplementations(facts.implementations), cloneScopes(facts.scopes),
      cloneModuleAliases(facts.moduleAliases), cloneImports(facts.imports),
      cloneLocalExports(facts.localExports), cloneDeferredMembers(facts.deferredMembers),
      cloneLabels(facts.labels), cloneControlTransfers(facts.controlTransfers),
      cloneShadowTargets(facts.shadowTargets),
      cloneClosureFreeVariables(facts.closureFreeVariables),
      cloneExplicitClosureCaptures(facts.explicitClosureCaptures),
      cloneGenericParameters(facts.genericParameters),
      cloneCallableParameters(facts.callableParameters),
      cloneOwnerLocalBindings(facts.ownerLocalBindings), cloneFailedLookups(facts.failedLookups)));
}

ImmutableBindingMetadata ImmutableBindingMetadata::clone() const {
  zc::Vector<BoundOwnerBody> ownerBodies;
  for (const auto& ownerBody : impl->ownerBodies) { ownerBodies.add(ownerBody.clone()); }
  return ImmutableBindingMetadata(zc::heap<Impl>(
      impl->context, impl->revision, impl->fingerprint.clone(), impl->skeleton.clone(),
      zc::mv(ownerBodies), impl->allocationPlan.clone(), cloneNodeScopes(impl->nodeScopes.asPtr()),
      cloneNodeBindings(impl->nodeBindings.asPtr()), cloneSelfTypes(impl->selfTypes.asPtr()),
      cloneThisBindings(impl->thisBindings.asPtr()), cloneDefinitions(impl->definitions.asPtr()),
      cloneImplementations(impl->implementations.asPtr()), cloneScopes(impl->scopes.asPtr()),
      cloneModuleAliases(impl->moduleAliases.asPtr()), cloneImports(impl->imports.asPtr()),
      cloneLocalExports(impl->localExports.asPtr()),
      cloneDeferredMembers(impl->deferredMembers.asPtr()), cloneLabels(impl->labels.asPtr()),
      cloneControlTransfers(impl->controlTransfers.asPtr()),
      cloneShadowTargets(impl->shadowTargets.asPtr()),
      cloneClosureFreeVariables(impl->closureFreeVariables.asPtr()),
      cloneExplicitClosureCaptures(impl->explicitClosureCaptures.asPtr()),
      cloneGenericParameters(impl->genericParameters.asPtr()),
      cloneCallableParameters(impl->callableParameters.asPtr()),
      cloneOwnerLocalBindings(impl->ownerLocalBindings.asPtr()),
      cloneFailedLookups(impl->failedLookups.asPtr())));
}

identity::SemanticContextBrand ImmutableBindingMetadata::semanticContext() const noexcept {
  return impl->context;
}

query::DatabaseRevision ImmutableBindingMetadata::revision() const noexcept {
  return impl->revision;
}

const identity::ContextFingerprint& ImmutableBindingMetadata::fingerprint() const noexcept {
  return impl->fingerprint;
}

const identity::ModuleKey& ImmutableBindingMetadata::module() const noexcept {
  return impl->skeleton.module();
}

const BoundModuleSkeleton& ImmutableBindingMetadata::skeleton() const noexcept {
  return impl->skeleton;
}

zc::ArrayPtr<const BoundOwnerBody> ImmutableBindingMetadata::ownerBodies() const noexcept {
  return impl->ownerBodies.asPtr();
}

const ModuleBindingAllocationPlan& ImmutableBindingMetadata::allocationPlan() const noexcept {
  return impl->allocationPlan;
}

zc::Maybe<const BoundOwnerBody&> ImmutableBindingMetadata::ownerBody(
    const StableOwnerBodyQueryKey& owner) const noexcept {
  for (const auto& body : ownerBodies()) {
    if (sameOwner(body.owner(), owner)) { return body; }
  }
  return zc::none;
}

zc::ArrayPtr<const NodeScopeFact> ImmutableBindingMetadata::nodeScopes() const noexcept {
  return impl->nodeScopes.asPtr();
}
zc::ArrayPtr<const BindingResolution> ImmutableBindingMetadata::nodeBindings() const noexcept {
  return impl->nodeBindings.asPtr();
}
zc::ArrayPtr<const BoundSelfType> ImmutableBindingMetadata::selfTypes() const noexcept {
  return impl->selfTypes.asPtr();
}
zc::ArrayPtr<const BoundThis> ImmutableBindingMetadata::thisBindings() const noexcept {
  return impl->thisBindings.asPtr();
}
zc::ArrayPtr<const DefinitionFact> ImmutableBindingMetadata::definitions() const noexcept {
  return impl->definitions.asPtr();
}
zc::ArrayPtr<const ImplBindingFact> ImmutableBindingMetadata::implementations() const noexcept {
  return impl->implementations.asPtr();
}
zc::ArrayPtr<const ScopeRecord> ImmutableBindingMetadata::scopes() const noexcept {
  return impl->scopes.asPtr();
}
zc::ArrayPtr<const ModuleAliasBindingFact> ImmutableBindingMetadata::moduleAliases()
    const noexcept {
  return impl->moduleAliases.asPtr();
}
zc::ArrayPtr<const ImportBindingFact> ImmutableBindingMetadata::imports() const noexcept {
  return impl->imports.asPtr();
}
zc::ArrayPtr<const LocalExportFact> ImmutableBindingMetadata::localExports() const noexcept {
  return impl->localExports.asPtr();
}
zc::ArrayPtr<const DeferredMemberFact> ImmutableBindingMetadata::deferredMembers() const noexcept {
  return impl->deferredMembers.asPtr();
}
zc::ArrayPtr<const LabelFact> ImmutableBindingMetadata::labels() const noexcept {
  return impl->labels.asPtr();
}
zc::ArrayPtr<const ControlTransferFact> ImmutableBindingMetadata::controlTransfers()
    const noexcept {
  return impl->controlTransfers.asPtr();
}
zc::ArrayPtr<const ShadowTargetFact> ImmutableBindingMetadata::shadowTargets() const noexcept {
  return impl->shadowTargets.asPtr();
}
zc::ArrayPtr<const ClosureFreeVariableFact> ImmutableBindingMetadata::closureFreeVariables()
    const noexcept {
  return impl->closureFreeVariables.asPtr();
}
zc::ArrayPtr<const ExplicitClosureCaptureFact> ImmutableBindingMetadata::explicitClosureCaptures()
    const noexcept {
  return impl->explicitClosureCaptures.asPtr();
}
zc::ArrayPtr<const GenericParameterFact> ImmutableBindingMetadata::genericParameters()
    const noexcept {
  return impl->genericParameters.asPtr();
}
zc::ArrayPtr<const CallableParameterFact> ImmutableBindingMetadata::callableParameters()
    const noexcept {
  return impl->callableParameters.asPtr();
}
zc::ArrayPtr<const OwnerLocalBindingFact> ImmutableBindingMetadata::ownerLocalBindings()
    const noexcept {
  return impl->ownerLocalBindings.asPtr();
}
zc::ArrayPtr<const MaterializedFailedLookupFact> ImmutableBindingMetadata::failedLookups()
    const noexcept {
  return impl->failedLookups.asPtr();
}

bool ImmutableBindingMetadata::matches(identity::SemanticContextBrand context,
                                       query::DatabaseRevision revision,
                                       const identity::ContextFingerprint& fingerprint,
                                       const BoundModuleSkeleton& sourceSkeleton,
                                       zc::ArrayPtr<const BoundOwnerBody> sourceOwnerBodies,
                                       const MaterializedBindingFacts& facts) const {
  if (semanticContext() != context || this->revision() != revision ||
      this->fingerprint().digest() != fingerprint.digest() || !(skeleton() == sourceSkeleton) ||
      ownerBodies().size() != sourceOwnerBodies.size() ||
      !ModuleBindingAllocationPlanner::verify(skeleton(), ownerBodies(), allocationPlan())) {
    return false;
  }
  for (size_t index = 0; index < ownerBodies().size(); ++index) {
    if (!(ownerBodies()[index] == sourceOwnerBodies[index])) { return false; }
  }
  return nodeScopes().size() == facts.nodeScopes.size() &&
         nodeBindings().size() == facts.nodeBindings.size() &&
         selfTypes().size() == facts.selfTypes.size() &&
         thisBindings().size() == facts.thisBindings.size() &&
         definitions().size() == facts.definitions.size() &&
         implementations().size() == facts.implementations.size() &&
         scopes().size() == facts.scopes.size() &&
         moduleAliases().size() == facts.moduleAliases.size() &&
         imports().size() == facts.imports.size() &&
         localExports().size() == facts.localExports.size() &&
         deferredMembers().size() == facts.deferredMembers.size() &&
         labels().size() == facts.labels.size() &&
         controlTransfers().size() == facts.controlTransfers.size() &&
         shadowTargets().size() == facts.shadowTargets.size() &&
         closureFreeVariables().size() == facts.closureFreeVariables.size() &&
         explicitClosureCaptures().size() == facts.explicitClosureCaptures.size() &&
         genericParameters().size() == facts.genericParameters.size() &&
         callableParameters().size() == facts.callableParameters.size() &&
         ownerLocalBindings().size() == facts.ownerLocalBindings.size() &&
         failedLookups().size() == facts.failedLookups.size();
}

}  // namespace zomlang::compiler::binder
