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

#include "zomlang/compiler/ownership/ownership-event-overlay.h"

#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::ownership {

namespace {

zc::Maybe<zc::Array<uint8_t>> encodeFunctionOverlay(
    const OwnershipFunctionEventOverlay& overlay,
    const identity::SemanticIdentityRegistrySet& registries) {
  identity::CanonicalEncoder encoder;
  auto owner = registries.definitions().lookup(overlay.owner);
  if (owner == zc::none) return zc::none;
  ZC_IF_SOME(key, owner) {
    auto bytes = key.encode();
    encoder.encodeByteString(bytes.asPtr());
  }
  encoder.encodeSequenceSize(overlay.slots.size());
  for (const auto& slot : overlay.slots) {
    if (!slot.key.location.block.isValid()) return zc::none;
    encoder.encodeUint32(slot.key.location.block.ordinal());
    encoder.encodeUint32(slot.key.location.statementIndex);
    encoder.encodeUint32(slot.key.operandOrdinal);
    encoder.encodeUint8(static_cast<uint8_t>(slot.stage));
    encoder.encodeSequenceSize(slot.roles.size());
    for (auto role : slot.roles) { encoder.encodeUint8(static_cast<uint8_t>(role)); }
  }
  return encoder.finish();
}

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

class RegistryIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit RegistryIdentityResolver(
      const identity::SemanticIdentityRegistrySet& registries) noexcept
      : registries(registries) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = registries.modules().lookup(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = registries.definitions().lookup(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.encode());
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
  const identity::SemanticIdentityRegistrySet& registries;
};

template <typename VerifiedValue>
ir::IrOperationResult<VerifiedValue> rejectOwnership(
    ir::IrFailurePhase phase, ir::IrFailureKind kind, identity::ModuleId module,
    zc::Maybe<identity::DefId> definition, const identity::SemanticIdentityRegistrySet& registries,
    uint32_t ordinal, zc::Vector<uint32_t>&& fieldPath = zc::Vector<uint32_t>()) {
  RegistryIdentityResolver identities(registries);
  if (definition == zc::none) {
    zc::Vector<identity::IdentityInvariant> failures;
    failures.add(invalidIdentity(identity::IdentityAllocationPhase::Definition, ordinal));
    auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedValue>::identityInvariantRejected(zc::mv(values));
    }
    ZC_UNREACHABLE
  }
  identity::DefId owner;
  ZC_IF_SOME(value, definition) { owner = value; }
  auto fallback = ir::IrFailureFallbackContext::from(phase, ir::IrFailureOwner::definition(owner));
  ZC_IREQUIRE(fallback != zc::none, "Ownership event overlay failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, phase, kind, ir::IrFailureOwner::definition(owner),
      zc::mv(noSite), ir::IrFailureDetail::none(), zc::mv(noSpan), zc::mv(fieldPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, identities);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<VerifiedValue>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
      failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    } else {
      failures.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
    }
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedValue>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

zc::Maybe<identity::DefId> firstFunctionDefinition(const mir::VerifiedBuiltMir& builtMir) {
  if (builtMir.functions().size() != 0) return builtMir.functions()[0].owner;
  return zc::none;
}

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

bool lessEventKey(const MirEventKey& left, const MirEventKey& right) noexcept {
  if (left.location.block.ordinal() != right.location.block.ordinal())
    return left.location.block.ordinal() < right.location.block.ordinal();
  if (left.location.statementIndex != right.location.statementIndex)
    return left.location.statementIndex < right.location.statementIndex;
  return left.operandOrdinal < right.operandOrdinal;
}

void sortRoles(zc::Vector<OwnershipEventRole>& roles) {
  for (size_t index = 1; index < roles.size(); ++index) {
    auto current = roles[index];
    size_t insertion = index;
    while (insertion > 0 &&
           static_cast<uint8_t>(current) < static_cast<uint8_t>(roles[insertion - 1])) {
      roles[insertion] = roles[insertion - 1];
      --insertion;
    }
    roles[insertion] = current;
  }
}

void sortSlots(zc::Vector<MirEventSlot>& slots) {
  for (auto& slot : slots) { sortRoles(slot.roles); }
  for (size_t index = 1; index < slots.size(); ++index) {
    auto current = zc::mv(slots[index]);
    size_t insertion = index;
    while (insertion > 0 && lessEventKey(current.key, slots[insertion - 1].key)) {
      slots[insertion] = zc::mv(slots[insertion - 1]);
      --insertion;
    }
    slots[insertion] = zc::mv(current);
  }
}

void sortFunctions(zc::Vector<OwnershipFunctionEventOverlay>& functions,
                   const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<zc::Array<uint8_t>> keys;
  for (const auto& function : functions) {
    auto key = registries.definitions().lookup(function.owner);
    if (key == zc::none) return;
    ZC_IF_SOME(value, key) { keys.add(value.encode()); }
  }
  for (size_t index = 1; index < functions.size(); ++index) {
    auto currentFunction = zc::mv(functions[index]);
    auto currentKey = zc::mv(keys[index]);
    size_t insertion = index;
    while (insertion > 0 && lessBytes(currentKey.asPtr(), keys[insertion - 1].asPtr())) {
      functions[insertion] = zc::mv(functions[insertion - 1]);
      keys[insertion] = zc::mv(keys[insertion - 1]);
      --insertion;
    }
    functions[insertion] = zc::mv(currentFunction);
    keys[insertion] = zc::mv(currentKey);
  }
}

void addSlot(zc::Vector<MirEventSlot>& slots, mir::MirBlockId block, uint32_t statementIndex,
             uint32_t operandOrdinal, OwnershipEventStage stage,
             zc::Vector<OwnershipEventRole>&& roles) {
  if (roles.size() == 0) return;
  slots.add(MirEventSlot{MirEventKey{MirEventLocation{block, statementIndex}, operandOrdinal},
                         stage, zc::mv(roles)});
}

zc::Maybe<zc::Vector<OwnershipFunctionEventOverlay>> buildFunctions(
    const mir::VerifiedBuiltMir& builtMir,
    const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<OwnershipFunctionEventOverlay> functions;
  for (const auto& function : builtMir.functions()) {
    zc::Vector<MirEventSlot> slots;
    for (const auto& block : function.blocks) {
      uint32_t statementIndex = 0;
      for (const auto& statement : block.statements) {
        switch (statement.kind()) {
          case mir::MirStatementKind::Assign: {
            const auto& operand = statement.assignmentValue().value.useValue().operand;
            zc::Vector<OwnershipEventRole> operandRoles;
            switch (operand.kind()) {
              case mir::MirOperandKind::Copy:
                operandRoles.add(OwnershipEventRole::OperandCopy);
                break;
              case mir::MirOperandKind::Move:
                operandRoles.add(OwnershipEventRole::OperandMove);
                break;
              case mir::MirOperandKind::Constant:
                operandRoles.add(OwnershipEventRole::ConstantOperand);
                break;
            }
            addSlot(slots, block.id, statementIndex, 1, OwnershipEventStage::Source,
                    zc::mv(operandRoles));
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            addSlot(slots, block.id, statementIndex, 0, OwnershipEventStage::Effect,
                    zc::mv(effectRoles));
            zc::Vector<OwnershipEventRole> commitRoles;
            commitRoles.add(OwnershipEventRole::DestinationWrite);
            addSlot(slots, block.id, statementIndex, 2, OwnershipEventStage::Commit,
                    zc::mv(commitRoles));
            break;
          }
          case mir::MirStatementKind::StorageLive: {
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            effectRoles.add(OwnershipEventRole::StorageLive);
            addSlot(slots, block.id, statementIndex, 0, OwnershipEventStage::Effect,
                    zc::mv(effectRoles));
            break;
          }
          case mir::MirStatementKind::StorageDead: {
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            effectRoles.add(OwnershipEventRole::StorageDead);
            addSlot(slots, block.id, statementIndex, 0, OwnershipEventStage::Effect,
                    zc::mv(effectRoles));
            break;
          }
          case mir::MirStatementKind::BorrowCreation: {
            zc::Vector<OwnershipEventRole> sourceRoles;
            sourceRoles.add(OwnershipEventRole::OperandRead);
            addSlot(slots, block.id, statementIndex, 1, OwnershipEventStage::Source,
                    zc::mv(sourceRoles));
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            effectRoles.add(OwnershipEventRole::BorrowIssue);
            addSlot(slots, block.id, statementIndex, 0, OwnershipEventStage::Effect,
                    zc::mv(effectRoles));
            zc::Vector<OwnershipEventRole> commitRoles;
            commitRoles.add(OwnershipEventRole::DestinationWrite);
            addSlot(slots, block.id, statementIndex, 2, OwnershipEventStage::Commit,
                    zc::mv(commitRoles));
            break;
          }
          case mir::MirStatementKind::SetDiscriminant: {
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            addSlot(slots, block.id, statementIndex, 0, OwnershipEventStage::Effect,
                    zc::mv(effectRoles));
            zc::Vector<OwnershipEventRole> commitRoles;
            commitRoles.add(OwnershipEventRole::DestinationWrite);
            commitRoles.add(OwnershipEventRole::SetDiscriminant);
            addSlot(slots, block.id, statementIndex, 1, OwnershipEventStage::Commit,
                    zc::mv(commitRoles));
            break;
          }
          case mir::MirStatementKind::Deinitialize: {
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            effectRoles.add(OwnershipEventRole::Deinitialize);
            addSlot(slots, block.id, statementIndex, 0, OwnershipEventStage::Effect,
                    zc::mv(effectRoles));
            break;
          }
        }
        ++statementIndex;
      }
      zc::Vector<OwnershipEventRole> termEffectRoles;
      termEffectRoles.add(OwnershipEventRole::Operation);
      if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
        ZC_IF_SOME(value, block.terminator.returnValue().value) {
          zc::Vector<OwnershipEventRole> operandRoles;
          switch (value.kind()) {
            case mir::MirOperandKind::Copy:
              operandRoles.add(OwnershipEventRole::OperandCopy);
              break;
            case mir::MirOperandKind::Move:
              operandRoles.add(OwnershipEventRole::OperandMove);
              break;
            case mir::MirOperandKind::Constant:
              operandRoles.add(OwnershipEventRole::ConstantOperand);
              break;
          }
          addSlot(slots, block.id, statementIndex, 1, OwnershipEventStage::Source,
                  zc::mv(operandRoles));
        }
      }
      addSlot(slots, block.id, statementIndex, 0, OwnershipEventStage::Effect,
              zc::mv(termEffectRoles));
    }
    sortSlots(slots);
    functions.add(OwnershipFunctionEventOverlay{function.owner, zc::mv(slots)});
  }
  sortFunctions(functions, registries);
  return functions;
}

}  // namespace

zc::Maybe<zc::Array<uint8_t>> OwnershipEventOverlayCodec::encode(
    const identity::SemanticContextFingerprint& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const checker::checked::CheckedFactsRevision& checkedFactsRevision,
    const mir::MirRevisionId& builtRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  if (expandedModuleKey.size() == 0) return zc::none;
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.ownership-event-overlay.v3";
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0x00);
  encoder.encodeDigest(contextFingerprint.digest());
  encoder.encodeByteString(expandedModuleKey);
  encoder.encodeDigest(checkedFactsRevision.digest());
  encoder.encodeDigest(builtRevision.digest());
  encoder.encodeSequenceSize(canonicalFunctions.size());
  for (const auto& function : canonicalFunctions) {
    if (function.size() == 0) return zc::none;
    encoder.encodeByteString(function.asPtr());
  }
  return encoder.finish();
}

zc::Maybe<OwnershipEventOverlayRevision> OwnershipEventOverlayCodec::compute(
    const identity::SemanticContextFingerprint& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const checker::checked::CheckedFactsRevision& checkedFactsRevision,
    const mir::MirRevisionId& builtRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  auto bytes = encode(contextFingerprint, expandedModuleKey, checkedFactsRevision, builtRevision,
                      canonicalFunctions);
  if (bytes == zc::none) return zc::none;
  ZC_IF_SOME(value, bytes) {
    auto digest = identity::sha256(value.asPtr());
    ZC_IF_SOME(hash, digest) { return OwnershipEventOverlayRevision::fromDigest(hash); }
  }
  return zc::none;
}

ir::IrOperationResult<OwnershipEventOverlayCandidate> OwnershipEventOverlayBuilder::build(
    const mir::VerifiedBuiltMir& builtMir,
    const identity::SemanticIdentityRegistrySet& registries) {
  const auto module = builtMir.module();
  auto functions = buildFunctions(builtMir, registries);
  if (functions == zc::none) {
    return rejectOwnership<OwnershipEventOverlayCandidate>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidOwnershipProof,
        module, firstFunctionDefinition(builtMir), registries, 0);
  }
  ZC_IF_SOME(value, functions) {
    return ir::IrOperationResult<OwnershipEventOverlayCandidate>::verified(
        OwnershipEventOverlayCandidate(
            builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
            builtMir.checkedFactsRevision(), builtMir.revision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedOwnershipEventOverlay> OwnershipEventOverlayVerifier::verify(
    OwnershipEventOverlayCandidate&& candidate, const mir::VerifiedBuiltMir& builtMir,
    const identity::SemanticIdentityRegistrySet& registries) {
  const auto module = builtMir.module();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.checkedFactsRevision.digest() != builtMir.checkedFactsRevision().digest() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.functions.size() != builtMir.functions().size()) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InputRevisionMismatch,
        module, firstFunctionDefinition(builtMir), registries, 0);
  }
  auto expectedFunctions = buildFunctions(builtMir, registries);
  if (expectedFunctions == zc::none) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidOwnershipProof,
        module, firstFunctionDefinition(builtMir), registries, 0);
  }
  zc::Vector<zc::Array<uint8_t>> recomputedRecords;
  ZC_IF_SOME(expected, expectedFunctions) {
    if (expected.size() != candidate.functions.size()) {
      return rejectOwnership<VerifiedOwnershipEventOverlay>(
          ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::AdditionalFact, module,
          firstFunctionDefinition(builtMir), registries, 0);
    }
    for (size_t index = 0; index < expected.size(); ++index) {
      const auto& expectedFunction = expected[index];
      const auto& candidateFunction = candidate.functions[index];
      if (expectedFunction.owner != candidateFunction.owner) {
        return rejectOwnership<VerifiedOwnershipEventOverlay>(
            ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidFact, module,
            expectedFunction.owner, registries, static_cast<uint32_t>(index + 1));
      }
      auto expectedEncoded = encodeFunctionOverlay(expectedFunction, registries);
      auto candidateEncoded = encodeFunctionOverlay(candidateFunction, registries);
      if (expectedEncoded == zc::none || candidateEncoded == zc::none) {
        return rejectOwnership<VerifiedOwnershipEventOverlay>(
            ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::CanonicalCodecMismatch,
            module, expectedFunction.owner, registries, static_cast<uint32_t>(index + 1));
      }
      zc::Array<uint8_t> expectedBytes;
      zc::Array<uint8_t> candidateBytes;
      ZC_IF_SOME(value, expectedEncoded) { expectedBytes = zc::mv(value); }
      ZC_IF_SOME(value, candidateEncoded) { candidateBytes = zc::mv(value); }
      if (expectedBytes.asPtr() != candidateBytes.asPtr()) {
        return rejectOwnership<VerifiedOwnershipEventOverlay>(
            ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::CanonicalCodecMismatch,
            module, expectedFunction.owner, registries, static_cast<uint32_t>(index + 1));
      }
      recomputedRecords.add(zc::mv(expectedBytes));
    }
  }
  auto moduleKey = registries.modules().lookup(builtMir.module());
  if (moduleKey == zc::none) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::MissingRequiredFact,
        module, firstFunctionDefinition(builtMir), registries, 0);
  }
  zc::Array<uint8_t> expandedModuleKey;
  ZC_IF_SOME(key, moduleKey) { expandedModuleKey = key.encode(); }
  zc::Maybe<OwnershipEventOverlayRevision> revision = OwnershipEventOverlayCodec::compute(
      candidate.contextFingerprint, expandedModuleKey.asPtr(), candidate.checkedFactsRevision,
      candidate.builtRevision, recomputedRecords.asPtr());
  if (revision == zc::none) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::CanonicalCodecMismatch,
        module, firstFunctionDefinition(builtMir), registries, 0);
  }
  ZC_IF_SOME(value, revision) {
    auto impl = zc::heap<VerifiedOwnershipEventOverlay::Impl>(
        candidate.semanticContext, candidate.contextFingerprint.clone(), candidate.module,
        candidate.checkedFactsRevision, candidate.builtRevision, zc::mv(candidate.functions),
        value);
    return ir::IrOperationResult<VerifiedOwnershipEventOverlay>::verified(
        VerifiedOwnershipEventOverlay(zc::mv(impl)));
  }
  ZC_UNREACHABLE
}

struct VerifiedOwnershipEventOverlay::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::SemanticContextFingerprint&& contextFingerprint, identity::ModuleId module,
       checker::checked::CheckedFactsRevision checkedFactsRevision,
       mir::MirRevisionId builtRevision, zc::Vector<OwnershipFunctionEventOverlay>&& functions,
       OwnershipEventOverlayRevision revision) noexcept
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        module(module),
        checkedFactsRevision(checkedFactsRevision),
        builtRevision(builtRevision),
        functions(zc::mv(functions)),
        revision(revision) {}

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  checker::checked::CheckedFactsRevision checkedFactsRevision;
  mir::MirRevisionId builtRevision;
  zc::Vector<OwnershipFunctionEventOverlay> functions;
  OwnershipEventOverlayRevision revision;
};

VerifiedOwnershipEventOverlay::VerifiedOwnershipEventOverlay(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedOwnershipEventOverlay::~VerifiedOwnershipEventOverlay() noexcept(false) = default;
VerifiedOwnershipEventOverlay::VerifiedOwnershipEventOverlay(
    VerifiedOwnershipEventOverlay&&) noexcept = default;
VerifiedOwnershipEventOverlay& VerifiedOwnershipEventOverlay::operator=(
    VerifiedOwnershipEventOverlay&&) noexcept = default;

identity::SemanticContextBrand VerifiedOwnershipEventOverlay::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::SemanticContextFingerprint& VerifiedOwnershipEventOverlay::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId VerifiedOwnershipEventOverlay::module() const noexcept { return impl->module; }
const checker::checked::CheckedFactsRevision& VerifiedOwnershipEventOverlay::checkedFactsRevision()
    const noexcept {
  return impl->checkedFactsRevision;
}
const mir::MirRevisionId& VerifiedOwnershipEventOverlay::builtRevision() const noexcept {
  return impl->builtRevision;
}
zc::ArrayPtr<const OwnershipFunctionEventOverlay> VerifiedOwnershipEventOverlay::functions()
    const noexcept {
  return impl->functions.asPtr();
}
const OwnershipEventOverlayRevision& VerifiedOwnershipEventOverlay::revision() const noexcept {
  return impl->revision;
}

}  // namespace zomlang::compiler::ownership
