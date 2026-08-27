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

#include "zomlang/compiler/ownership/facts/refs.h"

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
  ZC_IREQUIRE(fallback != zc::none, "Reference definition failure fallback must be legal");
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

bool inputsMatch(const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
                 const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return movePaths.semanticContext() == builtMir.semanticContext() &&
         movePaths.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         movePaths.module() == builtMir.module() &&
         movePaths.builtRevision().digest() == builtMir.revision().digest() &&
         movePaths.overlayRevision().digest() == overlay.revision().digest() &&
         loans.semanticContext() == builtMir.semanticContext() &&
         loans.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         loans.module() == builtMir.module() &&
         loans.builtRevision().digest() == builtMir.revision().digest() &&
         loans.overlayRevision().digest() == overlay.revision().digest() &&
         loans.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest();
}

bool hasEntryRoot(const VerifiedOwnershipEventOverlay& overlay, identity::DefId owner,
                  uint32_t ordinal) {
  for (const auto& function : overlay.functions()) {
    if (function.owner != owner) continue;
    size_t matches = 0;
    for (const auto& slot : function.slots) {
      if (slot.key.location.point.kind() != MirPointKind::Entry ||
          slot.key.operandOrdinal != ordinal || slot.stage != OwnershipEventStage::Commit ||
          slot.roles.size() != 1 || slot.roles[0] != OwnershipEventRole::EntryRoot) {
        continue;
      }
      ++matches;
    }
    return matches == 1;
  }
  return false;
}

bool hasDirectRootParameter(const driver::borrow_evidence::VerifiedBorrowEvidence& evidence,
                            identity::DefId owner, uint32_t parameter) {
  size_t matches = 0;
  for (const auto& summary : evidence.localSummaries()) {
    if (summary.callable != owner) continue;
    if (summary.returnRelation.tag() != checker::borrow::BorrowReturnRelationTag::DirectRoot ||
        summary.returnRelation.source().tag() != checker::borrow::BorrowInputRegionTag::Parameter ||
        summary.returnRelation.source().parameterIndex() != parameter) {
      return false;
    }
    bool directInput = false;
    for (const auto& input : summary.directInputs) {
      if (input.tag() == checker::borrow::BorrowInputRegionTag::Parameter &&
          input.parameterIndex() == parameter) {
        directInput = true;
      }
    }
    if (!directInput) return false;
    ++matches;
  }
  return matches == 1;
}

zc::Maybe<MirEventKey> returnedFrom(const mir::MirFunction& function,
                                    const mir::MirPlace& destination,
                                    const VerifiedOwnershipEventOverlay& overlay) {
  zc::Maybe<MirEventKey> result;
  for (const auto& block : function.blocks) {
    if (block.terminator.kind() != mir::MirTerminatorKind::Return ||
        block.terminator.returnValue().value == zc::none) {
      continue;
    }
    ZC_IF_SOME(value, block.terminator.returnValue().value) {
      if ((value.kind() != mir::MirOperandKind::Copy &&
           value.kind() != mir::MirOperandKind::Move) ||
          !samePlace(value.place(), destination)) {
        continue;
      }
      const auto transferRole = value.kind() == mir::MirOperandKind::Copy
                                    ? OwnershipEventRole::OperandCopy
                                    : OwnershipEventRole::OperandMove;
      const MirEventKey event{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0};
      bool matches = false;
      for (const auto& overlayFunction : overlay.functions()) {
        if (overlayFunction.owner != function.owner) continue;
        size_t slots = 0;
        for (const auto& slot : overlayFunction.slots) {
          if (slot.key != event || slot.stage != OwnershipEventStage::Source ||
              slot.roles.size() != 2 || slot.roles[0] != OwnershipEventRole::OperandRead ||
              slot.roles[1] != transferRole) {
            continue;
          }
          ++slots;
        }
        matches = slots == 1;
        break;
      }
      if (!matches || result != zc::none) return zc::none;
      result = event;
    }
  }
  return result;
}

zc::Maybe<ReferenceLivePoints> livePoints(const MirEventKey& introduction,
                                          const MirEventKey& returned) {
  if (introduction.location.owner != returned.location.owner ||
      introduction.location.point.kind() != MirPointKind::BeforeStatement ||
      returned.location.point.kind() != MirPointKind::BeforeTerminator) {
    return zc::none;
  }
  const auto& commit = introduction.location.point.beforeStatementValue();
  const auto& returnedPoint = returned.location.point.beforeTerminatorValue();
  return ReferenceLivePoints{
      OwnershipPoint::afterEvent(introduction),
      OwnershipPoint::cfg(MirPoint::afterStatement(commit.block, commit.ordinal)),
      OwnershipPoint::cfg(MirPoint::beforeTerminator(returnedPoint.block)),
      OwnershipPoint::beforeEvent(returned), OwnershipPoint::afterEvent(returned)};
}

zc::Maybe<uint32_t> parameterOrigin(const mir::MirFunction& function, const mir::MirPlace& source) {
  zc::Maybe<const mir::MirLocalDeclaration&> sourceLocal;
  for (const auto& local : function.locals) {
    if (local.id != source.local()) continue;
    if (sourceLocal != zc::none) return zc::none;
    sourceLocal = local;
  }
  if (sourceLocal == zc::none) return zc::none;
  ZC_IF_SOME(local, sourceLocal) {
    if (local.kind == mir::MirLocalKind::Parameter) {
      for (uint32_t ordinal = 0; ordinal < function.locals.size(); ++ordinal) {
        if (function.locals[ordinal].id == local.id) return ordinal;
      }
      return zc::none;
    }
    if (local.kind != mir::MirLocalKind::UserLocal) return zc::none;
  }
  zc::Maybe<uint32_t> origin;
  for (const auto& block : function.blocks) {
    for (const auto& statement : block.statements) {
      if (statement.kind() != mir::MirStatementKind::Assign) continue;
      const auto& assignment = statement.assignmentValue();
      if (assignment.initialization != mir::MirInitializationKind::Initialize ||
          assignment.destination.local() != source.local() ||
          assignment.destination.projections().size() != 0 ||
          assignment.value.kind() != mir::MirRvalueKind::Use) {
        continue;
      }
      const auto& operand = assignment.value.useValue().operand;
      if ((operand.kind() != mir::MirOperandKind::Copy &&
           operand.kind() != mir::MirOperandKind::Move) ||
          operand.place().projections().size() != 0) {
        return zc::none;
      }
      zc::Maybe<uint32_t> parameter;
      for (uint32_t ordinal = 0; ordinal < function.locals.size(); ++ordinal) {
        const auto& candidate = function.locals[ordinal];
        if (candidate.id == operand.place().local() &&
            candidate.kind == mir::MirLocalKind::Parameter) {
          parameter = ordinal;
        }
      }
      if (parameter == zc::none || origin != zc::none) return zc::none;
      origin = parameter;
    }
  }
  return origin;
}

zc::Maybe<MirEventKey> localOrigin(const mir::MirFunction& function, const mir::MirPlace& source) {
  if (source.projections().size() != 0) return zc::none;
  zc::Maybe<const mir::MirLocalDeclaration&> sourceLocal;
  for (const auto& local : function.locals) {
    if (local.id != source.local()) continue;
    if (sourceLocal != zc::none) return zc::none;
    sourceLocal = local;
  }
  if (sourceLocal == zc::none) return zc::none;
  ZC_IF_SOME(local, sourceLocal) {
    if (local.kind != mir::MirLocalKind::UserLocal) return zc::none;
  }
  zc::Maybe<MirEventKey> origin;
  for (const auto& block : function.blocks) {
    for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
      const auto& statement = block.statements[ordinal];
      if (statement.kind() != mir::MirStatementKind::StorageLive ||
          statement.storageLocal() != source.local()) {
        continue;
      }
      if (origin != zc::none) return zc::none;
      origin =
          MirEventKey{MirLocation{function.owner, MirPoint::beforeStatement(block.id, ordinal)}, 0};
    }
  }
  return origin;
}

zc::Maybe<zc::Vector<ReferenceDefinition>> derive(
    const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const driver::borrow_evidence::VerifiedBorrowEvidence& evidence) {
  zc::Vector<ReferenceDefinition> definitions;
  for (const auto& loan : loans.loans()) {
    zc::Maybe<const mir::MirFunction&> sourceFunction;
    for (const auto& function : builtMir.functions()) {
      if (function.owner == loan.owner) sourceFunction = function;
    }
    if (sourceFunction == zc::none) return zc::none;
    zc::Maybe<MirEventKey> returned;
    ZC_IF_SOME(function, sourceFunction) {
      returned = returnedFrom(function, loan.destination.place, overlay);
    }
    if (returned == zc::none) continue;
    const bool isParameterReborrow =
        loan.source.place.projections().size() == 1 &&
        loan.source.place.projections()[0].kind() == mir::MirProjectionKind::Dereference;
    const bool isLocalBorrow = loan.source.place.projections().size() == 0;
    if (!isParameterReborrow && !isLocalBorrow) return zc::none;
    if (loan.destination.place.projections().size() != 0) return zc::none;
    MirEventKey entryEvent{MirLocation{loan.owner, MirPoint::entry()}, 0};
    zc::OneOf<ParameterReferenceOrigin, LocalReferenceOrigin> detail{LocalReferenceOrigin{}};
    if (isParameterReborrow) {
      zc::Maybe<uint32_t> parameterOrdinal;
      ZC_IF_SOME(function, sourceFunction) {
        parameterOrdinal = parameterOrigin(function, loan.source.place);
      }
      if (parameterOrdinal == zc::none) return zc::none;
      ZC_IF_SOME(ordinal, parameterOrdinal) {
        if (!hasEntryRoot(overlay, loan.owner, ordinal) ||
            !hasDirectRootParameter(evidence, loan.owner, ordinal)) {
          return zc::none;
        }
        entryEvent = MirEventKey{MirLocation{loan.owner, MirPoint::entry()}, ordinal};
        detail = ParameterReferenceOrigin{ordinal};
      }
    } else {
      zc::Maybe<MirEventKey> localEntry;
      ZC_IF_SOME(function, sourceFunction) {
        localEntry = localOrigin(function, loan.source.place);
      }
      if (localEntry == zc::none) return zc::none;
      ZC_IF_SOME(event, localEntry) { entryEvent = event; }
    }
    ZC_IF_SOME(returnEvent, returned) {
      auto points = livePoints(loan.commit, returnEvent);
      if (points == zc::none) return zc::none;
      ZC_IF_SOME(value, points) {
        definitions.add(ReferenceDefinition{
            loan.owner, loan.commit, loan.issue,
            ReferenceInputOrigin{entryEvent, loan.activeFrom, detail,
                                 MovePathKey{loan.source.owner, loan.source.place.clone()}},
            returnEvent, MovePathKey{loan.destination.owner, loan.destination.place.clone()},
            zc::mv(value)});
      }
    }
  }
  return definitions;
}

bool sameDefinitions(zc::ArrayPtr<const ReferenceDefinition> left,
                     zc::ArrayPtr<const ReferenceDefinition> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].owner != right[index].owner ||
        left[index].introduction != right[index].introduction ||
        left[index].loan != right[index].loan ||
        left[index].origin.entry != right[index].origin.entry ||
        left[index].origin.activation != right[index].origin.activation ||
        left[index].origin.detail != right[index].origin.detail ||
        left[index].origin.referent.owner != right[index].origin.referent.owner ||
        !samePlace(left[index].origin.referent.place, right[index].origin.referent.place) ||
        left[index].returned != right[index].returned ||
        left[index].destination.owner != right[index].destination.owner ||
        !samePlace(left[index].destination.place, right[index].destination.place) ||
        left[index].livePoints.afterCommit != right[index].livePoints.afterCommit ||
        left[index].livePoints.afterCommitCfg != right[index].livePoints.afterCommitCfg ||
        left[index].livePoints.beforeReturnCfg != right[index].livePoints.beforeReturnCfg ||
        left[index].livePoints.beforeReturn != right[index].livePoints.beforeReturn ||
        left[index].livePoints.afterReturn != right[index].livePoints.afterReturn) {
      return false;
    }
  }
  return true;
}

}  // namespace

ReferenceDefinitionCandidate::ReferenceDefinitionCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
    zc::Vector<ReferenceDefinition>&& definitions) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      definitions(zc::mv(definitions)) {}

struct VerifiedReferenceDefinitions::Impl final {
  explicit Impl(ReferenceDefinitionCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}
  ReferenceDefinitionCandidate candidate;
};

VerifiedReferenceDefinitions::VerifiedReferenceDefinitions(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedReferenceDefinitions::~VerifiedReferenceDefinitions() noexcept(false) = default;
VerifiedReferenceDefinitions::VerifiedReferenceDefinitions(
    VerifiedReferenceDefinitions&&) noexcept = default;
VerifiedReferenceDefinitions& VerifiedReferenceDefinitions::operator=(
    VerifiedReferenceDefinitions&&) noexcept = default;
identity::SemanticContextBrand VerifiedReferenceDefinitions::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedReferenceDefinitions::contextFingerprint()
    const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedReferenceDefinitions::module() const noexcept {
  return impl->candidate.module;
}
const mir::MirRevisionId& VerifiedReferenceDefinitions::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedReferenceDefinitions::overlayRevision()
    const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision&
VerifiedReferenceDefinitions::borrowEvidenceRevision() const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const ReferenceDefinition> VerifiedReferenceDefinitions::definitions() const noexcept {
  return impl->candidate.definitions.asPtr();
}

ir::IrOperationResult<ReferenceDefinitionCandidate> ReferenceDefinitionBuilder::build(
    const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(movePaths, loans, builtMir, overlay)) {
    return reject<ReferenceDefinitionCandidate>(builtMir, identities,
                                                ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  const auto evidence = builtMir.borrowEvidence();
  if (!evidence.isResolved() ||
      evidence.evidence().revision().digest() != builtMir.borrowEvidenceRevision().digest()) {
    return reject<ReferenceDefinitionCandidate>(builtMir, identities,
                                                ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto definitions = derive(movePaths, loans, builtMir, overlay, evidence.evidence());
  if (definitions == zc::none) {
    return reject<ReferenceDefinitionCandidate>(builtMir, identities,
                                                ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  ZC_IF_SOME(value, definitions) {
    return ir::IrOperationResult<ReferenceDefinitionCandidate>::verified(
        ReferenceDefinitionCandidate(builtMir.semanticContext(),
                                     builtMir.contextFingerprint().clone(), builtMir.module(),
                                     builtMir.revision(), overlay.revision(),
                                     builtMir.borrowEvidenceRevision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedReferenceDefinitions> ReferenceDefinitionVerifier::verify(
    ReferenceDefinitionCandidate&& candidate, const VerifiedMovePaths& movePaths,
    const VerifiedLoanFacts& loans, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(movePaths, loans, builtMir, overlay)) {
    return reject<VerifiedReferenceDefinitions>(builtMir, identities,
                                                ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  const auto evidence = builtMir.borrowEvidence();
  if (!evidence.isResolved() ||
      evidence.evidence().revision().digest() != builtMir.borrowEvidenceRevision().digest()) {
    return reject<VerifiedReferenceDefinitions>(builtMir, identities,
                                                ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto expected = derive(movePaths, loans, builtMir, overlay, evidence.evidence());
  if (expected == zc::none ||
      !sameDefinitions(candidate.definitions, ZC_ASSERT_NONNULL(expected))) {
    return reject<VerifiedReferenceDefinitions>(builtMir, identities,
                                                ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedReferenceDefinitions>::verified(VerifiedReferenceDefinitions(
      zc::heap<VerifiedReferenceDefinitions::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
