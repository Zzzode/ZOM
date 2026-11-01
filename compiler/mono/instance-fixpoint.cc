// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mono/instance-fixpoint.h"

#include "zc/core/debug.h"
#include "compiler/ir/instance-identity.h"
#include "compiler/mono/instance-cloner.h"
#include "compiler/type/semantic-type-data.h"
#include "compiler/type/type-substitution.h"

namespace zomlang::compiler::mono {
namespace {

using checker::checked::CanonicalSubstitutionId;
using checker::checked::SubstitutionData;
using checker::checked::WitnessArgumentsId;
using identity::DefId;
using identity::SourceSpan;
using ir::InstanceId;
using mir::MirFunction;
using type::semantic::PrimitiveKind;
using type::semantic::TypeDataTag;
using type::semantic::TypeParameterTypeData;
using type::semantic::TypeSubstitution;
using type::SemanticTypeLookup;
using type::SemanticTypeStore;

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

bool hasKey(zc::ArrayPtr<const identity::GenericParameterKey> keys,
            const identity::GenericParameterKey& candidate) {
  for (const auto& key : keys) {
    if (key == candidate) { return true; }
  }
  return false;
}

/// \brief Demand record queued for fixpoint expansion with its expansion chain.
struct PendingDemand final {
  InstanceId instance;
  SourceSpan span;
  zc::Vector<InstanceId> chain;
};

struct FixpointState final {
  const MonoEvidenceProvider& provider;
  SemanticTypeStore& semanticTypes;
  uint64_t instanceBudget;
  uint64_t substitutionNodeBudget;
  uint64_t substitutedNodeCount = 0;
  zc::Vector<MonoRawInstance> collected;

  explicit FixpointState(const MonoEvidenceProvider& provider, SemanticTypeStore& semanticTypes,
                         uint64_t instanceBudget, uint64_t substitutionNodeBudget)
      : provider(provider),
        semanticTypes(semanticTypes),
        instanceBudget(instanceBudget),
        substitutionNodeBudget(substitutionNodeBudget) {}

  bool seen(const InstanceId& instance) const noexcept {
    return indexOf(instance) != zc::none;
  }

  zc::Maybe<size_t> indexOf(const InstanceId& instance) const noexcept {
    for (size_t index = 0; index < collected.size(); ++index) {
      if (collected[index].identity == instance) { return index; }
    }
    return zc::none;
  }

  /// \brief Finds the current expansion-chain member with the same definition.
  zc::Maybe<size_t> chainIndexOf(DefId definition,
                                 zc::ArrayPtr<const InstanceId> chain) const noexcept {
    for (size_t index = 0; index < chain.size(); ++index) {
      if (chain[index].definition() == definition) { return index; }
    }
    return zc::none;
  }

  zc::Vector<TypeSubstitution> buildMapping(const SubstitutionData& data) const {
    zc::Vector<TypeSubstitution> mapping;
    ZC_IREQUIRE(data.parameters.size() == data.arguments.size(),
                "verified substitution record must pair every parameter with an argument");
    for (size_t index = 0; index < data.parameters.size(); ++index) {
      mapping.add(TypeSubstitution{data.parameters[index].clone(), data.arguments[index]});
    }
    return mapping;
  }

  /// \brief Produces the canonical instance identity frame for one edge.
  zc::Maybe<zc::Array<uint8_t>> frameFor(const InstanceId& instance) const {
    auto definitionKey = provider.definitionKey(instance.definition());
    auto substitution = provider.substitutionRecord(instance.substitution());
    auto witness = provider.witnessRecord(instance.witnesses());
    if (definitionKey == zc::none || substitution == zc::none || witness == zc::none) {
      return zc::none;
    }
    return ir::encodeInstanceIdentity(ZC_ASSERT_NONNULL(definitionKey).asPtr(),
                                      ZC_ASSERT_NONNULL(substitution),
                                      ZC_ASSERT_NONNULL(witness));
  }
};

/// \brief Scans one body for Instance edges and appends demands in CFG order.
void collectEdges(const MirFunction& function, zc::Vector<PendingDemand>& demands,
                  zc::ArrayPtr<const InstanceId> chain) {
  for (const auto& block : function.blocks) {
    if (block.terminator.kind() != mir::MirTerminatorKind::Call) { continue; }
    const auto& call = block.terminator.callValue();
    if (call.callee.kind() != mir::MirCallableIdentityKind::Instance) { continue; }
    zc::Vector<InstanceId> nextChain;
    for (const auto& item : chain) { nextChain.add(item); }
    demands.add(PendingDemand{call.callee.instanceValue(), block.terminator.sourceSpan().clone(),
                              zc::mv(nextChain)});
  }
}

/// \brief Expands one demanded instance: guards, clones, and substitutes.
///
/// The result is one of: a queued raw instance index, a typed failure, or a
/// "shared existing instance" skip signalled by returning zc::none with
/// sharedExisting set.
struct DemandOutcome final {
  zc::Maybe<MonoCycleError> cycle;
  zc::Maybe<MonoBudgetError> budget;
  zc::Maybe<MonoUnresolvedEdgeError> unresolved;
  bool sharedExisting = false;
};

DemandOutcome demand(PendingDemand&& demand, FixpointState& state,
                     zc::Vector<PendingDemand>& queue) {
  DemandOutcome outcome;
  if (state.seen(demand.instance)) {
    outcome.sharedExisting = true;
    return outcome;
  }
  ZC_IF_SOME(chainPosition, state.chainIndexOf(demand.instance.definition(), demand.chain)) {
    // The head definition repeats through a strictly nested instantiation.
    zc::Vector<InstanceId> errorChain;
    for (size_t index = chainPosition; index < demand.chain.size(); ++index) {
      errorChain.add(demand.chain[index]);
    }
    errorChain.add(demand.instance);
    outcome.cycle = MonoCycleError{demand.chain[chainPosition], zc::mv(errorChain),
                                   zc::mv(demand.span)};
    return outcome;
  }
  if (state.collected.size() + 1 > state.instanceBudget) {
    zc::Vector<InstanceId> chain;
    for (const auto& item : demand.chain) { chain.add(item); }
    chain.add(demand.instance);
    outcome.budget = MonoBudgetError{
        demand.instance, zc::mv(chain), state.collected.size() + 1,
        state.substitutedNodeCount, state.instanceBudget, state.substitutionNodeBudget,
        zc::mv(demand.span)};
    return outcome;
  }
  auto templateFunction = state.provider.templateFor(demand.instance.definition());
  auto keys = state.provider.genericParameterKeys(demand.instance.definition());
  auto data = state.provider.substitution(demand.instance.substitution());
  auto frame = state.frameFor(demand.instance);
  if (templateFunction == zc::none || keys == zc::none || data == zc::none || frame == zc::none) {
    outcome.unresolved = MonoUnresolvedEdgeError{demand.instance, zc::mv(demand.span)};
    return outcome;
  }
  auto body = cloneTemplateFunction(ZC_ASSERT_NONNULL(templateFunction));
  auto mapping = state.buildMapping(ZC_ASSERT_NONNULL(data));
  uint64_t bodyNodes = 0;
  auto substitutionFailure =
      substituteFunctionTypes(body, mapping.asPtr(), state.semanticTypes, bodyNodes);
  if (substitutionFailure != zc::none) {
    outcome.unresolved = MonoUnresolvedEdgeError{demand.instance, zc::mv(demand.span)};
    return outcome;
  }
  state.substitutedNodeCount += bodyNodes;
  if (state.substitutedNodeCount > state.substitutionNodeBudget) {
    zc::Vector<InstanceId> chain;
    for (const auto& item : demand.chain) { chain.add(item); }
    chain.add(demand.instance);
    outcome.budget = MonoBudgetError{
        demand.instance, zc::mv(chain), state.collected.size() + 1, state.substitutedNodeCount,
        state.instanceBudget, state.substitutionNodeBudget, zc::mv(demand.span)};
    return outcome;
  }
  state.collected.add(MonoRawInstance{demand.instance, zc::mv(body),
                                      zc::mv(ZC_ASSERT_NONNULL(frame)), zc::mv(demand.span),
                                      bodyNodes});
  zc::Vector<InstanceId> nextChain;
  for (const auto& item : demand.chain) { nextChain.add(item); }
  nextChain.add(demand.instance);
  collectEdges(state.collected[state.collected.size() - 1].body, queue, nextChain.asPtr());
  return outcome;
}

}  // namespace

MonoFixpointResult collectInstances(zc::ArrayPtr<const MirFunction* const> roots,
                                    const MonoEvidenceProvider& provider,
                                    SemanticTypeStore& semanticTypes, uint64_t instanceBudget,
                                    uint64_t substitutionNodeBudget) {
  FixpointState state(provider, semanticTypes, instanceBudget, substitutionNodeBudget);
  zc::Vector<PendingDemand> queue;
  zc::ArrayPtr<const InstanceId> emptyChain;
  for (const auto* root : roots) {
    collectEdges(*root, queue, emptyChain);
  }
  size_t head = 0;
  while (head < queue.size()) {
    auto pending = zc::mv(queue[head]);
    ++head;
    auto outcome = demand(zc::mv(pending), state, queue);
    ZC_IF_SOME(cycle, outcome.cycle) {
      return zc::mv(cycle);
    }
    ZC_IF_SOME(budget, outcome.budget) {
      return zc::mv(budget);
    }
    ZC_IF_SOME(unresolved, outcome.unresolved) {
      return zc::mv(unresolved);
    }
  }
  return zc::mv(state.collected);
}

void sortInstancesByIdentity(zc::Vector<MonoRawInstance>& instances) noexcept {
  // Insertion sort: fixpoint collection already runs in deterministic order and
  // instance counts are bounded by the fixed budget.
  for (size_t index = 1; index < instances.size(); ++index) {
    auto current = zc::mv(instances[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           lessBytes(current.identityFrame.asPtr(),
                     instances[insertion - 1].identityFrame.asPtr())) {
      instances[insertion] = zc::mv(instances[insertion - 1]);
      --insertion;
    }
    instances[insertion] = zc::mv(current);
  }
}

bool instancesAreFrameSorted(zc::ArrayPtr<const MonoRawInstance> instances) noexcept {
  for (size_t index = 1; index < instances.size(); ++index) {
    if (lessBytes(instances[index].identityFrame.asPtr(),
                  instances[index - 1].identityFrame.asPtr())) {
      return false;
    }
  }
  return true;
}

namespace {

/// \brief Accumulates the first carrier or triviality failure in slot order.
class BodyPredicateWalker final {
public:
  BodyPredicateWalker(zc::ArrayPtr<const identity::GenericParameterKey> calleeKeys,
                      const SemanticTypeStore& semanticTypes)
      : calleeKeys(calleeKeys), semanticTypes(semanticTypes) {}

  bool inspectType(identity::SemanticTypeId id, MonoBodyRejectionKind& failure) {
    auto lookup = semanticTypes.get(id);
    if (!lookup.is<SemanticTypeLookup>()) {
      failure = MonoBodyRejectionKind::NonScalarCarrier;
      return false;
    }
    const auto& data = lookup.get<SemanticTypeLookup>().data();
    if (data.tag() == TypeDataTag::Reference) {
      failure = MonoBodyRejectionKind::ReferenceCarrier;
      return false;
    }
    if (data.tag() == TypeDataTag::RawPointer) {
      failure = MonoBodyRejectionKind::ReferenceCarrier;
      return false;
    }
    if (data.is<TypeParameterTypeData>()) {
      if (hasKey(calleeKeys, data.get<TypeParameterTypeData>().parameter)) {
        failure = MonoBodyRejectionKind::ResidualGenericKey;
        return false;
      }
    }
    auto primitive = data.primitiveKind();
    if (primitive == zc::none ||
        (ZC_ASSERT_NONNULL(primitive) != PrimitiveKind::I32 &&
         ZC_ASSERT_NONNULL(primitive) != PrimitiveKind::Bool)) {
      failure = MonoBodyRejectionKind::NonScalarCarrier;
      return false;
    }
    return true;
  }

private:
  zc::ArrayPtr<const identity::GenericParameterKey> calleeKeys;
  const SemanticTypeStore& semanticTypes;
};

}  // namespace

zc::Maybe<MonoBodyRejection> validateInstanceBody(
    const MirFunction& body, zc::ArrayPtr<const identity::GenericParameterKey> calleeKeys,
    const SemanticTypeStore& semanticTypes) noexcept {
  BodyPredicateWalker walker(calleeKeys, semanticTypes);
  MonoBodyRejectionKind failure{};
  bool rejected = false;
  forEachFunctionTypeSlot(body, [&](identity::SemanticTypeId id) {
    if (rejected) { return; }
    if (!walker.inspectType(id, failure)) { rejected = true; }
  });
  if (rejected) { return MonoBodyRejection{failure, 0, 0}; }
  for (uint32_t blockIndex = 0; blockIndex < body.blocks.size(); ++blockIndex) {
    const auto& block = body.blocks[blockIndex];
    for (uint32_t statementIndex = 0; statementIndex < block.statements.size(); ++statementIndex) {
      const auto kind = block.statements[statementIndex].kind();
      MonoBodyRejectionKind statementFailure{};
      if (kind == mir::MirStatementKind::BorrowCreation) {
        statementFailure = MonoBodyRejectionKind::BorrowCreationStatement;
      } else if (kind == mir::MirStatementKind::UnsafeScopeBoundary) {
        statementFailure = MonoBodyRejectionKind::UnsafeScopeBoundaryStatement;
      } else if (kind == mir::MirStatementKind::Deinitialize) {
        statementFailure = MonoBodyRejectionKind::DeinitializeStatement;
      } else {
        continue;
      }
      return MonoBodyRejection{statementFailure, blockIndex + 1, statementIndex + 1};
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
      const auto& call = block.terminator.callValue();
      if (call.effect.kind() != mir::MirCallEffectKind::NoActivation) {
        return MonoBodyRejection{MonoBodyRejectionKind::ReceiverActivationEffect,
                                 blockIndex + 1, 0};
      }
    }
  }
  return zc::none;
}

zc::Maybe<MonoEdgeRejection> validateCallEdges(
    const MirFunction& body, const MonoEvidenceProvider& provider,
    zc::FunctionParam<bool(InstanceId)> isMember) noexcept {
  for (uint32_t blockIndex = 0; blockIndex < body.blocks.size(); ++blockIndex) {
    const auto& block = body.blocks[blockIndex];
    if (block.terminator.kind() != mir::MirTerminatorKind::Call) { continue; }
    const auto& call = block.terminator.callValue();
    if (call.callee.kind() != mir::MirCallableIdentityKind::Instance) { continue; }
    const auto instance = call.callee.instanceValue();
    auto keys = provider.genericParameterKeys(instance.definition());
    auto data = provider.substitution(instance.substitution());
    if (keys == zc::none || data == zc::none) {
      return MonoEdgeRejection{MonoEdgeRejectionKind::UnresolvedCallee, blockIndex + 1};
    }
    const auto& substitution = ZC_ASSERT_NONNULL(data);
    const auto& expectedKeys = ZC_ASSERT_NONNULL(keys);
    if (substitution.arguments.size() != expectedKeys.size() ||
        substitution.parameters.size() != expectedKeys.size()) {
      return MonoEdgeRejection{MonoEdgeRejectionKind::ArityMismatch, blockIndex + 1};
    }
    for (size_t index = 0; index < expectedKeys.size(); ++index) {
      if (substitution.parameters[index] != expectedKeys[index]) {
        return MonoEdgeRejection{MonoEdgeRejectionKind::ParameterKeyMismatch, blockIndex + 1};
      }
    }
    if (!isMember(instance)) {
      return MonoEdgeRejection{MonoEdgeRejectionKind::EdgeNotClosed, blockIndex + 1};
    }
  }
  return zc::none;
}

}  // namespace zomlang::compiler::mono
