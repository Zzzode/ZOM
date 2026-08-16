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

#include "zomlang/compiler/ownership/facts/paths.h"

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
  ZC_IREQUIRE(fallback != zc::none, "Move-path failure fallback must be legal");
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
    zc::Vector<ir::IrFailureFact> failureFacts;
    failureFacts.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    auto facts = ir::SortedIrInvariantFailureFacts::from(zc::mv(failureFacts));
    ZC_IF_SOME(values, facts) {
      return ir::IrOperationResult<Result>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

bool matchesPlace(const mir::MirFunction& function, const mir::MirPlace& place) {
  if (!place.hasConsistentTypeChain()) return false;
  for (const auto& local : function.locals) {
    if (local.id != place.local() || place.rootType() != local.type) continue;
    for (const auto& projection : place.projections()) {
      if (!projection.isStructurallyValid()) return false;
      if (projection.kind() != mir::MirProjectionKind::Index) continue;
      bool foundIndex = false;
      for (const auto& index : function.locals) {
        if (index.id == projection.indexValue().index) {
          foundIndex = true;
          break;
        }
      }
      if (!foundIndex) return false;
    }
    return place.projections().size() != 0 || place.resultType() == local.type;
  }
  return false;
}

bool validOperand(const mir::MirFunction& function, const mir::MirOperand& operand) {
  if (operand.kind() == mir::MirOperandKind::Constant) return true;
  return matchesPlace(function, operand.place());
}

bool validRvalue(const mir::MirFunction& function, const mir::MirRvalue& rvalue) {
  if (rvalue.kind() == mir::MirRvalueKind::Use) {
    return validOperand(function, rvalue.useValue().operand);
  }
  const auto& aggregate = rvalue.nominalAggregateValue();
  if (!aggregate.definition.isValid() || !aggregate.type.isValid()) return false;
  for (const auto& element : aggregate.elements) {
    if (!element.field.isValid() || element.operand.kind() != mir::MirOperandKind::Constant) {
      return false;
    }
  }
  return true;
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

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = zc::min(left.size(), right.size());
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

zc::Maybe<bool> lessKey(const MovePathKey& left, const MovePathKey& right,
                        const checker::CheckerIdentityAuthority& identities) {
  if (left.owner != right.owner) return zc::none;
  if (left.place.local() != right.place.local()) {
    return left.place.local().ordinal() < right.place.local().ordinal();
  }
  const auto lessProjection = [&](const mir::MirProjection& first,
                                  const mir::MirProjection& second) -> zc::Maybe<bool> {
    if (first.kind() != second.kind()) {
      return static_cast<uint8_t>(first.kind()) < static_cast<uint8_t>(second.kind());
    }
    if (sameProjection(first, second)) return false;
    switch (first.kind()) {
      case mir::MirProjectionKind::Field: {
        auto firstIdentity = identities.definition(first.fieldValue().field);
        auto secondIdentity = identities.definition(second.fieldValue().field);
        if (firstIdentity == zc::none || secondIdentity == zc::none) return zc::none;
        ZC_IF_SOME(firstValue, firstIdentity) {
          ZC_IF_SOME(secondValue, secondIdentity) {
            auto firstBytes = firstValue.key().encode();
            auto secondBytes = secondValue.key().encode();
            if (firstBytes.asPtr() == secondBytes.asPtr()) return zc::none;
            return lessBytes(firstBytes.asPtr(), secondBytes.asPtr());
          }
        }
        ZC_UNREACHABLE
      }
      case mir::MirProjectionKind::Index:
        return first.indexValue().index.ordinal() < second.indexValue().index.ordinal();
      case mir::MirProjectionKind::Dereference:
        return zc::none;
      case mir::MirProjectionKind::Downcast: {
        auto firstIdentity = identities.definition(first.downcastValue().variant);
        auto secondIdentity = identities.definition(second.downcastValue().variant);
        if (firstIdentity == zc::none || secondIdentity == zc::none) return zc::none;
        ZC_IF_SOME(firstValue, firstIdentity) {
          ZC_IF_SOME(secondValue, secondIdentity) {
            auto firstBytes = firstValue.key().encode();
            auto secondBytes = secondValue.key().encode();
            if (firstBytes.asPtr() == secondBytes.asPtr()) return zc::none;
            return lessBytes(firstBytes.asPtr(), secondBytes.asPtr());
          }
        }
        ZC_UNREACHABLE
      }
      case mir::MirProjectionKind::Subslice:
        if (first.subsliceValue().first != second.subsliceValue().first) {
          return first.subsliceValue().first < second.subsliceValue().first;
        }
        if (first.subsliceValue().pastLast != second.subsliceValue().pastLast) {
          return first.subsliceValue().pastLast < second.subsliceValue().pastLast;
        }
        return zc::none;
    }
    ZC_UNREACHABLE
  };
  const auto shared = zc::min(left.place.projections().size(), right.place.projections().size());
  for (size_t index = 0; index < shared; ++index) {
    auto less = lessProjection(left.place.projections()[index], right.place.projections()[index]);
    if (less == zc::none) return zc::none;
    ZC_IF_SOME(value, less) {
      if (value) return true;
      if (!sameProjection(left.place.projections()[index], right.place.projections()[index])) {
        return false;
      }
    }
  }
  return left.place.projections().size() < right.place.projections().size();
}

bool sortFacts(zc::Vector<MovePathFact>& facts,
               const checker::CheckerIdentityAuthority& identities) {
  for (size_t index = 1; index < facts.size(); ++index) {
    auto current = zc::mv(facts[index]);
    size_t insertion = index;
    while (insertion != 0) {
      auto less = lessKey(current.key, facts[insertion - 1].key, identities);
      if (less == zc::none) return false;
      ZC_IF_SOME(value, less) {
        if (!value) break;
      }
      facts[insertion] = zc::mv(facts[insertion - 1]);
      --insertion;
    }
    facts[insertion] = zc::mv(current);
  }
  return true;
}

bool validateFunction(const mir::MirFunction& function);

bool appendPlace(zc::Vector<mir::MirPlace>& paths, const mir::MirPlace& place) {
  if (!place.hasConsistentTypeChain()) return false;
  for (const auto& projection : place.projections()) {
    if (!projection.isStructurallyValid()) return false;
  }
  for (size_t length = 0; length <= place.projections().size(); ++length) {
    zc::Vector<mir::MirProjection> projections;
    for (size_t index = 0; index < length; ++index) {
      projections.add(place.projections()[index].clone());
    }
    const auto resultType =
        length == 0 ? place.rootType() : place.projections()[length - 1].resultType();
    auto prefix = mir::MirPlace(place.local(), place.rootType(), zc::mv(projections), resultType);
    bool found = false;
    for (const auto& existing : paths) {
      if (samePlace(existing, prefix)) {
        found = true;
        break;
      }
    }
    if (!found) paths.add(zc::mv(prefix));
  }
  return true;
}

zc::Maybe<MovePathKey> cloneKey(const MovePathKey& key) {
  return MovePathKey{key.owner, key.place.clone()};
}

bool placesConflict(const mir::MirPlace& first, const mir::MirPlace& second) {
  if (first.local() != second.local()) return false;
  const auto shared = zc::min(first.projections().size(), second.projections().size());
  for (size_t index = 0; index < shared; ++index) {
    const auto& firstProjection = first.projections()[index];
    const auto& secondProjection = second.projections()[index];
    if (sameProjection(firstProjection, secondProjection)) continue;
    if (firstProjection.kind() == mir::MirProjectionKind::Field &&
        secondProjection.kind() == mir::MirProjectionKind::Field) {
      return false;
    }
    if (firstProjection.kind() == mir::MirProjectionKind::Downcast &&
        secondProjection.kind() == mir::MirProjectionKind::Downcast) {
      return false;
    }
    if (firstProjection.kind() == mir::MirProjectionKind::Subslice &&
        secondProjection.kind() == mir::MirProjectionKind::Subslice) {
      const auto& firstSlice = firstProjection.subsliceValue();
      const auto& secondSlice = secondProjection.subsliceValue();
      if (firstSlice.pastLast <= secondSlice.first || secondSlice.pastLast <= firstSlice.first) {
        return false;
      }
    }
    return true;
  }
  return true;
}

zc::Maybe<MovePathFunction> deriveFunction(const mir::MirFunction& function,
                                           const checker::CheckerIdentityAuthority& identities) {
  if (!validateFunction(function)) return zc::none;
  zc::Vector<mir::MirPlace> paths;
  for (const auto& local : function.locals) {
    zc::Vector<mir::MirProjection> projections;
    if (!appendPlace(paths, mir::MirPlace(local.id, local.type, zc::mv(projections), local.type))) {
      return zc::none;
    }
  }
  for (const auto& block : function.blocks) {
    for (const auto& statement : block.statements) {
      switch (statement.kind()) {
        case mir::MirStatementKind::Assign: {
          const auto& assignment = statement.assignmentValue();
          if (!appendPlace(paths, assignment.destination)) return zc::none;
          if (assignment.value.kind() == mir::MirRvalueKind::Use) {
            const auto& operand = assignment.value.useValue().operand;
            if (operand.kind() != mir::MirOperandKind::Constant &&
                !appendPlace(paths, operand.place())) {
              return zc::none;
            }
          }
          break;
        }
        case mir::MirStatementKind::StorageLive:
        case mir::MirStatementKind::StorageDead:
          break;
        case mir::MirStatementKind::BorrowCreation: {
          const auto& borrow = statement.borrowCreationValue();
          if (!appendPlace(paths, borrow.destination) || !appendPlace(paths, borrow.source)) {
            return zc::none;
          }
          break;
        }
        case mir::MirStatementKind::SetDiscriminant:
          if (!appendPlace(paths, statement.setDiscriminantValue().destination)) return zc::none;
          break;
        case mir::MirStatementKind::Deinitialize:
          if (!appendPlace(paths, statement.deinitializeValue().destination)) return zc::none;
          break;
      }
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
      ZC_IF_SOME(operand, block.terminator.returnValue().value) {
        if (operand.kind() != mir::MirOperandKind::Constant &&
            !appendPlace(paths, operand.place())) {
          return zc::none;
        }
      }
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
      const auto& call = block.terminator.callValue();
      if (!appendPlace(paths, call.destination)) return zc::none;
      for (const auto& argument : call.arguments) {
        if (argument.kind() != mir::MirOperandKind::Constant &&
            !appendPlace(paths, argument.place())) {
          return zc::none;
        }
      }
    }
  }

  zc::Vector<MovePathFact> facts;
  for (const auto& path : paths) {
    zc::Maybe<MovePathKey> parent;
    if (path.projections().size() != 0) {
      zc::Vector<mir::MirProjection> parentProjections;
      for (size_t index = 0; index + 1 < path.projections().size(); ++index) {
        parentProjections.add(path.projections()[index].clone());
      }
      const auto parentType = path.projections().size() == 1
                                  ? path.rootType()
                                  : path.projections()[path.projections().size() - 2].resultType();
      auto parentPlace =
          mir::MirPlace(path.local(), path.rootType(), zc::mv(parentProjections), parentType);
      bool foundRoot = false;
      for (const auto& candidate : paths) {
        if (samePlace(candidate, parentPlace)) {
          foundRoot = true;
          break;
        }
      }
      if (!foundRoot) return zc::none;
      parent = MovePathKey{function.owner, zc::mv(parentPlace)};
    }
    facts.add(MovePathFact{MovePathKey{function.owner, path.clone()}, zc::mv(parent)});
  }
  if (!sortFacts(facts, identities)) return zc::none;
  zc::Vector<MovePathPair> pairs;
  for (size_t first = 0; first < facts.size(); ++first) {
    for (size_t second = first + 1; second < facts.size(); ++second) {
      if (!placesConflict(facts[first].key.place, facts[second].key.place)) continue;
      auto firstKey = cloneKey(facts[first].key);
      auto secondKey = cloneKey(facts[second].key);
      ZC_IF_SOME(firstValue, firstKey) {
        ZC_IF_SOME(secondValue, secondKey) {
          pairs.add(MovePathPair{zc::mv(firstValue), zc::mv(secondValue)});
        }
      }
    }
  }
  return MovePathFunction{function.owner, zc::mv(facts), zc::mv(pairs)};
}

bool validateFunction(const mir::MirFunction& function) {
  for (const auto& block : function.blocks) {
    for (const auto& statement : block.statements) {
      switch (statement.kind()) {
        case mir::MirStatementKind::Assign: {
          const auto& assignment = statement.assignmentValue();
          if (!matchesPlace(function, assignment.destination) ||
              !validRvalue(function, assignment.value)) {
            return false;
          }
          break;
        }
        case mir::MirStatementKind::StorageLive:
        case mir::MirStatementKind::StorageDead:
          break;
        case mir::MirStatementKind::BorrowCreation: {
          const auto& borrow = statement.borrowCreationValue();
          if (!matchesPlace(function, borrow.destination) ||
              !matchesPlace(function, borrow.source)) {
            return false;
          }
          break;
        }
        case mir::MirStatementKind::SetDiscriminant:
          if (!matchesPlace(function, statement.setDiscriminantValue().destination)) return false;
          break;
        case mir::MirStatementKind::Deinitialize:
          if (!matchesPlace(function, statement.deinitializeValue().destination)) return false;
          break;
      }
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
      ZC_IF_SOME(value, block.terminator.returnValue().value) {
        if (!validOperand(function, value)) return false;
      }
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
      const auto& call = block.terminator.callValue();
      if (!matchesPlace(function, call.destination)) return false;
      for (const auto& argument : call.arguments) {
        if (!validOperand(function, argument)) return false;
      }
    }
  }
  return true;
}

zc::Maybe<zc::Vector<MovePathFunction>> derive(
    const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities) {
  zc::Vector<MovePathFunction> functions;
  for (const auto& function : builtMir.functions()) {
    auto paths = deriveFunction(function, identities);
    if (paths == zc::none) return zc::none;
    ZC_IF_SOME(value, paths) { functions.add(zc::mv(value)); }
  }
  return functions;
}

bool sameFunctions(zc::ArrayPtr<const MovePathFunction> left,
                   zc::ArrayPtr<const MovePathFunction> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].owner != right[index].owner ||
        left[index].facts.size() != right[index].facts.size() ||
        left[index].conflicts.size() != right[index].conflicts.size()) {
      return false;
    }
    for (size_t fact = 0; fact < left[index].facts.size(); ++fact) {
      const auto& first = left[index].facts[fact];
      const auto& second = right[index].facts[fact];
      if (first.key.owner != second.key.owner || !samePlace(first.key.place, second.key.place) ||
          (first.parent == zc::none) != (second.parent == zc::none)) {
        return false;
      }
      ZC_IF_SOME(firstParent, first.parent) {
        ZC_IF_SOME(secondParent, second.parent) {
          if (firstParent.owner != secondParent.owner ||
              !samePlace(firstParent.place, secondParent.place)) {
            return false;
          }
        }
      }
    }
    for (size_t pair = 0; pair < left[index].conflicts.size(); ++pair) {
      const auto& first = left[index].conflicts[pair];
      const auto& second = right[index].conflicts[pair];
      if (first.first.owner != second.first.owner || first.second.owner != second.second.owner ||
          !samePlace(first.first.place, second.first.place) ||
          !samePlace(first.second.place, second.second.place)) {
        return false;
      }
    }
  }
  return true;
}

bool inputsMatch(const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return overlay.semanticContext() == builtMir.semanticContext() &&
         overlay.module() == builtMir.module() &&
         overlay.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         overlay.builtRevision().digest() == builtMir.revision().digest();
}

}  // namespace

MovePathCandidate::MovePathCandidate(identity::SemanticContextBrand semanticContext,
                                     identity::SemanticContextFingerprint&& contextFingerprint,
                                     identity::ModuleId module, mir::MirRevisionId builtRevision,
                                     OwnershipEventOverlayRevision overlayRevision,
                                     zc::Vector<MovePathFunction>&& functions) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      functions(zc::mv(functions)) {}

struct VerifiedMovePaths::Impl final {
  explicit Impl(MovePathCandidate&& candidate) noexcept
      : semanticContext(candidate.semanticContext),
        contextFingerprint(zc::mv(candidate.contextFingerprint)),
        module(candidate.module),
        builtRevision(candidate.builtRevision),
        overlayRevision(candidate.overlayRevision),
        functions(zc::mv(candidate.functions)) {}

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  zc::Vector<MovePathFunction> functions;
};

VerifiedMovePaths::VerifiedMovePaths(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedMovePaths::~VerifiedMovePaths() noexcept(false) = default;
VerifiedMovePaths::VerifiedMovePaths(VerifiedMovePaths&&) noexcept = default;
VerifiedMovePaths& VerifiedMovePaths::operator=(VerifiedMovePaths&&) noexcept = default;
identity::SemanticContextBrand VerifiedMovePaths::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::SemanticContextFingerprint& VerifiedMovePaths::contextFingerprint() const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId VerifiedMovePaths::module() const noexcept { return impl->module; }
const mir::MirRevisionId& VerifiedMovePaths::builtRevision() const noexcept {
  return impl->builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedMovePaths::overlayRevision() const noexcept {
  return impl->overlayRevision;
}
zc::ArrayPtr<const MovePathFunction> VerifiedMovePaths::functions() const noexcept {
  return impl->functions;
}

bool VerifiedMovePaths::conflicts(const MovePathKey& first,
                                  const MovePathKey& second) const noexcept {
  if (first.owner != second.owner) return false;
  for (const auto& function : impl->functions) {
    if (function.owner != first.owner) continue;
    bool hasFirst = false;
    bool hasSecond = false;
    for (const auto& fact : function.facts) {
      if (samePlace(fact.key.place, first.place)) hasFirst = true;
      if (samePlace(fact.key.place, second.place)) hasSecond = true;
    }
    if (!hasFirst || !hasSecond) return false;
    if (samePlace(first.place, second.place)) return true;
    for (const auto& pair : function.conflicts) {
      if ((samePlace(pair.first.place, first.place) &&
           samePlace(pair.second.place, second.place)) ||
          (samePlace(pair.first.place, second.place) &&
           samePlace(pair.second.place, first.place))) {
        return true;
      }
    }
    return false;
  }
  return false;
}

ir::IrOperationResult<MovePathCandidate> MovePathBuilder::build(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(builtMir, overlay)) {
    return reject<MovePathCandidate>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch,
                                     0);
  }
  auto functions = derive(builtMir, identities);
  if (functions == zc::none) {
    return reject<MovePathCandidate>(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof,
                                     1);
  }
  ZC_IF_SOME(value, functions) {
    return ir::IrOperationResult<MovePathCandidate>::verified(MovePathCandidate(
        builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
        builtMir.revision(), overlay.revision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedMovePaths> MovePathVerifier::verify(
    MovePathCandidate&& candidate, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.module != builtMir.module() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      !inputsMatch(builtMir, overlay)) {
    return reject<VerifiedMovePaths>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch,
                                     0);
  }
  auto expected = derive(builtMir, identities);
  if (expected == zc::none || !sameFunctions(candidate.functions, ZC_ASSERT_NONNULL(expected))) {
    return reject<VerifiedMovePaths>(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof,
                                     1);
  }
  return ir::IrOperationResult<VerifiedMovePaths>::verified(
      VerifiedMovePaths(zc::heap<VerifiedMovePaths::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
