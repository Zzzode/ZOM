// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "zomlang/compiler/ownership/facts/resources.h"

#include "zomlang/compiler/ir/ir-diagnostic-adapter.h"

namespace zomlang::compiler::ownership::facts {
namespace {

identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
                                            uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
      identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

class AuthorityIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit AuthorityIdentityResolver(const checker::CheckerIdentityAuthority& identities) noexcept
      : identities(identities) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = identities.module(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = identities.definition(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
  }

private:
  const checker::CheckerIdentityAuthority& identities;
};

template <typename Result>
ir::IrOperationResult<Result> reject(const mir::VerifiedBuiltMir& builtMir,
                                     const checker::CheckerIdentityAuthority& identities,
                                     ir::IrFailureKind kind, uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Resource fact failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, ir::IrFailurePhase::OwnershipProofValidation, kind,
      ir::IrFailureOwner::definition(definition), zc::mv(noSite), ir::IrFailureDetail::none(),
      zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<Result>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<Result>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

bool sameProjection(const mir::MirProjection& left, const mir::MirProjection& right) {
  if (left.kind() != right.kind() || left.inputType() != right.inputType() ||
      left.resultType() != right.resultType()) {
    return false;
  }
  switch (left.kind()) {
    case mir::MirProjectionKind::Field:
      return left.fieldValue().field == right.fieldValue().field;
    case mir::MirProjectionKind::Index:
      return left.indexValue().index == right.indexValue().index;
    case mir::MirProjectionKind::Dereference:
      return true;
    case mir::MirProjectionKind::Downcast:
      return left.downcastValue().variant == right.downcastValue().variant;
    case mir::MirProjectionKind::Subslice:
      return left.subsliceValue().first == right.subsliceValue().first &&
             left.subsliceValue().pastLast == right.subsliceValue().pastLast;
  }
  return false;
}

bool samePlace(const mir::MirPlace& left, const mir::MirPlace& right) {
  if (left.local() != right.local() || left.rootType() != right.rootType() ||
      left.resultType() != right.resultType() ||
      left.projections().size() != right.projections().size()) {
    return false;
  }
  for (size_t index = 0; index < left.projections().size(); ++index) {
    if (!sameProjection(left.projections()[index], right.projections()[index])) return false;
  }
  return true;
}

bool positive(const OwnershipMarkerUseKey& key, const OwnershipFunctionEventOverlay& overlay) {
  for (const auto& use : overlay.markerUses) {
    if (use.key.event != key.event || use.key.marker != key.marker ||
        use.key.subject != key.subject ||
        use.key.markerPolicyRevision.digest() != key.markerPolicyRevision.digest() ||
        use.key.coherenceRevision.digest() != key.coherenceRevision.digest()) {
      continue;
    }
    return use.decision.is<OwnershipMarkerDecisionPositive>();
  }
  return false;
}

bool containsMovePath(const VerifiedMovePaths& movePaths, identity::DefId owner,
                      const mir::MirPlace& place) {
  for (const auto& function : movePaths.functions()) {
    if (function.owner != owner) continue;
    for (const auto& fact : function.facts) {
      if (fact.key.owner == owner && samePlace(fact.key.place, place)) return true;
    }
    return false;
  }
  return false;
}

bool inputsMatch(const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return movePaths.semanticContext() == builtMir.semanticContext() &&
         movePaths.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         movePaths.module() == builtMir.module() &&
         movePaths.builtRevision().digest() == builtMir.revision().digest() &&
         movePaths.overlayRevision().digest() == overlay.revision().digest() &&
         builtMir.semanticContext() == overlay.semanticContext() &&
         builtMir.contextFingerprint().digest() == overlay.contextFingerprint().digest() &&
         builtMir.module() == overlay.module() &&
         builtMir.checkedFactsRevision().digest() == overlay.checkedFactsRevision().digest() &&
         builtMir.revision().digest() == overlay.builtRevision().digest();
}

zc::Maybe<DropRequirement> requirement(const LogicalDropPlanComponent& component,
                                       const OwnershipFunctionEventOverlay& overlay) {
  const bool copy = positive(component.copyDecision, overlay);
  const bool linear = positive(component.linearDecision, overlay);
  if (component.dropAction != zc::none && copy) return zc::none;
  if (!copy && !linear) return DropRequirement::Logical;
  if (copy && linear) return DropRequirement::Linear;
  if (!copy && linear) return DropRequirement::LinearLogical;
  return zc::none;
}

bool sameActions(const zc::Maybe<LogicalDropAction>& left,
                 const zc::Maybe<LogicalDropAction>& right) {
  if (left == zc::none) return right == zc::none;
  if (right == zc::none) return false;
  ZC_IF_SOME(leftAction, left) {
    ZC_IF_SOME(rightAction, right) {
      if (leftAction.is<LogicalDropDeclaredAction>() !=
              rightAction.is<LogicalDropDeclaredAction>() ||
          leftAction.is<LogicalDropBuiltinAction>() != rightAction.is<LogicalDropBuiltinAction>()) {
        return false;
      }
      if (leftAction.is<LogicalDropDeclaredAction>()) {
        return leftAction.get<LogicalDropDeclaredAction>().deinitializer ==
               rightAction.get<LogicalDropDeclaredAction>().deinitializer;
      }
      if (leftAction.is<LogicalDropBuiltinAction>()) {
        return leftAction.get<LogicalDropBuiltinAction>().ownerType ==
               rightAction.get<LogicalDropBuiltinAction>().ownerType;
      }
      return leftAction.get<LogicalDropDynamicAction>().existentialType ==
             rightAction.get<LogicalDropDynamicAction>().existentialType;
    }
  }
  ZC_UNREACHABLE
}

bool sameMovePath(const MovePathKey& left, const MovePathKey& right) {
  return left.owner == right.owner && samePlace(left.place, right.place);
}

zc::Maybe<uint32_t> resourceAt(const zc::Vector<OwnershipResourceFact>& facts,
                               const zc::Vector<DropTransfer>& transfers,
                               const zc::Vector<CastResourceRoute>& castRoutes,
                               const MovePathKey& place) {
  for (uint32_t ordinal = 0; ordinal < facts.size(); ++ordinal) {
    MovePathKey current{facts[ordinal].subject.origin.owner,
                        facts[ordinal].subject.origin.place.clone()};
    for (const auto& transfer : transfers) {
      if (sameMovePath(current, transfer.from)) {
        current = MovePathKey{transfer.to.owner, transfer.to.place.clone()};
      }
    }
    for (const auto& route : castRoutes) {
      if (sameMovePath(current, route.from)) {
        current = MovePathKey{route.to.owner, route.to.place.clone()};
      }
    }
    if (sameMovePath(current, place)) return ordinal;
  }
  return zc::none;
}

struct MoveTransferInitialization final {
  MirEventKey event;
  mir::MirPlace source;
};

zc::Maybe<MoveTransferInitialization> moveTransferInitialization(const mir::MirFunction& function,
                                                                 const LogicalDropPlan& plan) {
  if (plan.initialization.location.owner != function.owner ||
      plan.initialization.location.point.kind() != MirPointKind::BeforeStatement ||
      plan.initialization.operandOrdinal != 2) {
    return zc::none;
  }
  const auto& point = plan.initialization.location.point.beforeStatementValue();
  for (const auto& block : function.blocks) {
    if (block.id != point.block || point.ordinal >= block.statements.size()) continue;
    const auto& statement = block.statements[point.ordinal];
    if (statement.kind() != mir::MirStatementKind::Assign) return zc::none;
    const auto& assignment = statement.assignmentValue();
    if (assignment.value.kind() != mir::MirRvalueKind::Use ||
        assignment.value.useValue().operand.kind() != mir::MirOperandKind::Move ||
        !samePlace(assignment.destination, plan.root)) {
      return zc::none;
    }
    return MoveTransferInitialization{
        MirEventKey{MirLocation{function.owner, MirPoint::beforeStatement(block.id, point.ordinal)},
                    0},
        assignment.value.useValue().operand.place().clone()};
  }
  return zc::none;
}

zc::Maybe<uint32_t> parameterEntryOrdinal(const mir::MirFunction& function,
                                          const mir::MirPlace& place) {
  if (place.projections().size() != 0) return zc::none;
  for (uint32_t ordinal = 0; ordinal < function.locals.size(); ++ordinal) {
    const auto& local = function.locals[ordinal];
    if (local.id == place.local() && local.kind == mir::MirLocalKind::Parameter &&
        local.type == place.rootType() && local.type == place.resultType()) {
      return ordinal;
    }
  }
  return zc::none;
}

bool isParameterRootTransfer(const LogicalDropPlan& plan, const LogicalDropPlanComponent& component,
                             const MoveTransferInitialization& transfer) {
  return transfer.source.projections().size() == 0 && samePlace(plan.root, component.place) &&
         component.valueType == transfer.source.resultType();
}

zc::Maybe<zc::Vector<OwnershipResourceFunction>> derive(
    const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  zc::Vector<OwnershipResourceFunction> functions;
  for (const auto& mirFunction : builtMir.functions()) {
    zc::Maybe<const OwnershipFunctionEventOverlay&> overlayFunction;
    for (const auto& candidate : overlay.functions()) {
      if (candidate.owner == mirFunction.owner) {
        overlayFunction = candidate;
        break;
      }
    }
    if (overlayFunction == zc::none) return zc::none;
    ZC_IF_SOME(value, overlayFunction) {
      zc::Vector<OwnershipResourceFact> facts;
      zc::Vector<DropTransfer> transfers;
      zc::Vector<CastResourceRoute> castRoutes;
      zc::Vector<DropPlan> dropPlans;
      for (uint32_t pass = 0; pass < 2; ++pass) {
        for (const auto& plan : value.logicalDropPlans) {
          if (plan.initialization.location.owner != mirFunction.owner ||
              !containsMovePath(movePaths, mirFunction.owner, plan.root)) {
            return zc::none;
          }
          auto transfer = moveTransferInitialization(mirFunction, plan);
          if ((pass == 0 && transfer != zc::none) || (pass == 1 && transfer == zc::none)) {
            continue;
          }
          zc::Maybe<uint32_t> parameterOrdinal;
          if (transfer != zc::none) {
            ZC_IF_SOME(transferValue, transfer) {
              parameterOrdinal = parameterEntryOrdinal(mirFunction, transferValue.source);
            }
          }
          zc::Vector<DropPlanComponent> planComponents;
          zc::Maybe<DropResourceSubject> rootSubject;
          for (const auto& component : plan.components) {
            if (!containsMovePath(movePaths, mirFunction.owner, component.place)) return zc::none;
            auto componentRequirement = requirement(component, value);
            if (componentRequirement == zc::none) return zc::none;
            ZC_IF_SOME(expectedRequirement, componentRequirement) {
              auto introduction = plan.initialization;
              auto origin = MovePathKey{mirFunction.owner, component.place.clone()};
              zc::Maybe<identity::SemanticTypeId> castOriginType;
              zc::Maybe<uint32_t> factOrdinal;
              if (transfer != zc::none) {
                ZC_IF_SOME(transferValue, transfer) {
                  const auto source = MovePathKey{mirFunction.owner, transferValue.source.clone()};
                  if (isParameterRootTransfer(plan, component, transferValue) &&
                      parameterOrdinal != zc::none) {
                    ZC_IF_SOME(entryOrdinal, parameterOrdinal) {
                      introduction = MirEventKey{MirLocation{mirFunction.owner, MirPoint::entry()},
                                                 entryOrdinal};
                      origin = MovePathKey{source.owner, source.place.clone()};
                      transfers.add(
                          DropTransfer{MovePathKey{mirFunction.owner, transferValue.source.clone()},
                                       MovePathKey{mirFunction.owner, component.place.clone()},
                                       transferValue.event});
                    }
                  } else {
                    auto resource = resourceAt(facts, transfers, castRoutes, source);
                    if (resource == zc::none) {
                      // A parameter root moved through a type-changing cast
                      // introduces the resource at the parameter entry and
                      // preserves its subject across the cast.
                      if (parameterOrdinal != zc::none &&
                          transferValue.source.projections().size() == 0 &&
                          samePlace(plan.root, component.place)) {
                        ZC_IF_SOME(entryOrdinal, parameterOrdinal) {
                          introduction = MirEventKey{
                              MirLocation{mirFunction.owner, MirPoint::entry()}, entryOrdinal};
                          origin = MovePathKey{source.owner, source.place.clone()};
                          castOriginType = transferValue.source.resultType();
                          castRoutes.add(CastResourceRoute{
                              DropResourceSubject{introduction,
                                                  MovePathKey{source.owner, source.place.clone()},
                                                  transferValue.source.resultType()},
                              MovePathKey{mirFunction.owner, transferValue.source.clone()},
                              MovePathKey{mirFunction.owner, component.place.clone()},
                              transferValue.event});
                        }
                      } else {
                        continue;
                      }
                    } else {
                      ZC_IF_SOME(resourceOrdinal, resource) {
                        const auto& sourceFact = facts[resourceOrdinal];
                        if (sourceFact.requirement != expectedRequirement ||
                            !sameActions(sourceFact.dropAction, component.dropAction)) {
                          return zc::none;
                        }
                        if (sourceFact.subject.originType != component.valueType) {
                          // Type-changing cast: preserve the resource subject
                          // across the cast and record the exact route.
                          castRoutes.add(CastResourceRoute{
                              sourceFact.subject.clone(),
                              MovePathKey{mirFunction.owner, transferValue.source.clone()},
                              MovePathKey{mirFunction.owner, component.place.clone()},
                              transferValue.event});
                        } else {
                          transfers.add(DropTransfer{
                              MovePathKey{mirFunction.owner, transferValue.source.clone()},
                              MovePathKey{mirFunction.owner, component.place.clone()},
                              transferValue.event});
                        }
                        factOrdinal = resourceOrdinal;
                      }
                    }
                  }
                }
              }
              if (factOrdinal == zc::none) {
                factOrdinal = static_cast<uint32_t>(facts.size());
                identity::SemanticTypeId subjectType = component.valueType;
                if (castOriginType != zc::none) subjectType = ZC_ASSERT_NONNULL(castOriginType);
                facts.add(OwnershipResourceFact{
                    DropResourceSubject{introduction, zc::mv(origin), subjectType},
                    expectedRequirement, component.dropAction, component.declarationOrdinal});
              }
              ZC_IF_SOME(ordinal, factOrdinal) {
                if (rootSubject == zc::none && samePlace(component.place, plan.root)) {
                  rootSubject = facts[ordinal].subject.clone();
                }
                planComponents.add(DropPlanComponent{ordinal, component.dropAction});
              }
            }
          }
          ZC_IF_SOME(subject, rootSubject) {
            dropPlans.add(DropPlan{zc::mv(subject), DropPlanMode::Closed, zc::mv(planComponents)});
          }
        }
      }
      functions.add(OwnershipResourceFunction{mirFunction.owner, zc::mv(facts), zc::mv(transfers),
                                              zc::mv(castRoutes), zc::mv(dropPlans)});
    }
  }
  return functions;
}

bool sameSubjects(const DropResourceSubject& left, const DropResourceSubject& right) {
  return left.introduction == right.introduction && left.origin.owner == right.origin.owner &&
         samePlace(left.origin.place, right.origin.place) && left.originType == right.originType;
}

bool sameFacts(const OwnershipResourceFact& left, const OwnershipResourceFact& right) {
  return sameSubjects(left.subject, right.subject) && left.requirement == right.requirement &&
         sameActions(left.dropAction, right.dropAction) &&
         left.declarationOrdinal == right.declarationOrdinal;
}

bool sameTransfers(const DropTransfer& left, const DropTransfer& right) {
  return left.from.owner == right.from.owner && samePlace(left.from.place, right.from.place) &&
         left.to.owner == right.to.owner && samePlace(left.to.place, right.to.place) &&
         left.event == right.event;
}

bool sameCastRoutes(const CastResourceRoute& left, const CastResourceRoute& right) {
  return sameSubjects(left.subject, right.subject) && left.from.owner == right.from.owner &&
         samePlace(left.from.place, right.from.place) && left.to.owner == right.to.owner &&
         samePlace(left.to.place, right.to.place) && left.event == right.event;
}

bool samePlanComponents(const DropPlanComponent& left, const DropPlanComponent& right) {
  return left.factOrdinal == right.factOrdinal && sameActions(left.action, right.action);
}

bool sameDropPlans(const DropPlan& left, const DropPlan& right) {
  if (!sameSubjects(left.subject, right.subject) || left.mode != right.mode ||
      left.components.size() != right.components.size()) {
    return false;
  }
  for (size_t index = 0; index < left.components.size(); ++index) {
    if (!samePlanComponents(left.components[index], right.components[index])) return false;
  }
  return true;
}

bool sameFunctions(zc::ArrayPtr<const OwnershipResourceFunction> left,
                   zc::ArrayPtr<const OwnershipResourceFunction> right) {
  if (left.size() != right.size()) return false;
  for (size_t functionIndex = 0; functionIndex < left.size(); ++functionIndex) {
    if (left[functionIndex].owner != right[functionIndex].owner ||
        left[functionIndex].facts.size() != right[functionIndex].facts.size() ||
        left[functionIndex].transfers.size() != right[functionIndex].transfers.size() ||
        left[functionIndex].castRoutes.size() != right[functionIndex].castRoutes.size() ||
        left[functionIndex].dropPlans.size() != right[functionIndex].dropPlans.size()) {
      return false;
    }
    for (size_t factIndex = 0; factIndex < left[functionIndex].facts.size(); ++factIndex) {
      if (!sameFacts(left[functionIndex].facts[factIndex], right[functionIndex].facts[factIndex])) {
        return false;
      }
    }
    for (size_t transferIndex = 0; transferIndex < left[functionIndex].transfers.size();
         ++transferIndex) {
      if (!sameTransfers(left[functionIndex].transfers[transferIndex],
                         right[functionIndex].transfers[transferIndex])) {
        return false;
      }
    }
    for (size_t routeIndex = 0; routeIndex < left[functionIndex].castRoutes.size(); ++routeIndex) {
      if (!sameCastRoutes(left[functionIndex].castRoutes[routeIndex],
                          right[functionIndex].castRoutes[routeIndex])) {
        return false;
      }
    }
    for (size_t planIndex = 0; planIndex < left[functionIndex].dropPlans.size(); ++planIndex) {
      if (!sameDropPlans(left[functionIndex].dropPlans[planIndex],
                         right[functionIndex].dropPlans[planIndex])) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

OwnershipResourceCandidate::OwnershipResourceCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    zc::Vector<OwnershipResourceFunction>&& functions) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      functions(zc::mv(functions)) {}

struct VerifiedOwnershipResourceFacts::Impl final {
  explicit Impl(OwnershipResourceCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}
  OwnershipResourceCandidate candidate;
};

VerifiedOwnershipResourceFacts::VerifiedOwnershipResourceFacts(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedOwnershipResourceFacts::~VerifiedOwnershipResourceFacts() noexcept(false) = default;
VerifiedOwnershipResourceFacts::VerifiedOwnershipResourceFacts(
    VerifiedOwnershipResourceFacts&&) noexcept = default;
VerifiedOwnershipResourceFacts& VerifiedOwnershipResourceFacts::operator=(
    VerifiedOwnershipResourceFacts&&) noexcept = default;
identity::SemanticContextBrand VerifiedOwnershipResourceFacts::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedOwnershipResourceFacts::contextFingerprint()
    const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedOwnershipResourceFacts::module() const noexcept {
  return impl->candidate.module;
}
const mir::MirRevisionId& VerifiedOwnershipResourceFacts::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedOwnershipResourceFacts::overlayRevision()
    const noexcept {
  return impl->candidate.overlayRevision;
}
zc::ArrayPtr<const OwnershipResourceFunction> VerifiedOwnershipResourceFacts::functions()
    const noexcept {
  return impl->candidate.functions.asPtr();
}

ir::IrOperationResult<OwnershipResourceCandidate> OwnershipResourceBuilder::build(
    const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(movePaths, builtMir, overlay)) {
    return reject<OwnershipResourceCandidate>(builtMir, identities,
                                              ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto functions = derive(movePaths, builtMir, overlay);
  if (functions == zc::none) {
    return reject<OwnershipResourceCandidate>(builtMir, identities,
                                              ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  ZC_IF_SOME(value, functions) {
    return ir::IrOperationResult<OwnershipResourceCandidate>::verified(OwnershipResourceCandidate(
        builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
        builtMir.revision(), overlay.revision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedOwnershipResourceFacts> OwnershipResourceVerifier::verify(
    OwnershipResourceCandidate&& candidate, const VerifiedMovePaths& movePaths,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      !inputsMatch(movePaths, builtMir, overlay)) {
    return reject<VerifiedOwnershipResourceFacts>(builtMir, identities,
                                                  ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto expected = derive(movePaths, builtMir, overlay);
  if (expected == zc::none || !sameFunctions(candidate.functions, ZC_ASSERT_NONNULL(expected))) {
    return reject<VerifiedOwnershipResourceFacts>(builtMir, identities,
                                                  ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedOwnershipResourceFacts>::verified(
      VerifiedOwnershipResourceFacts(
          zc::heap<VerifiedOwnershipResourceFacts::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
