// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/ir/ir-failure.h"

#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::ir {
namespace {

template <typename Enum>
constexpr bool enumInRange(Enum value, Enum first, Enum last) noexcept {
  return value >= first && value <= last;
}

bool isCapabilityKind(IrFailureKind kind) noexcept {
  return kind == IrFailureKind::UnsupportedTargetCapability ||
         kind == IrFailureKind::RecursiveInstantiation ||
         kind == IrFailureKind::InstantiationBudgetExceeded ||
         kind == IrFailureKind::OutputCreationFailed;
}

bool isOneOf(IrFailureKind kind, zc::ArrayPtr<const IrFailureKind> allowed) noexcept {
  for (const auto candidate : allowed) {
    if (candidate == kind) { return true; }
  }
  return false;
}

bool legalKind(IrRejectedBranch branch, IrFailurePhase phase, IrFailureKind kind) noexcept {
  if (branch == IrRejectedBranch::CapabilityRejected) {
    switch (phase) {
      case IrFailurePhase::Monomorphization:
        return kind == IrFailureKind::RecursiveInstantiation ||
               kind == IrFailureKind::InstantiationBudgetExceeded;
      case IrFailurePhase::TargetSelection:
        return kind == IrFailureKind::UnsupportedTargetCapability;
      case IrFailurePhase::ObjectEmission:
        return kind == IrFailureKind::OutputCreationFailed;
      default:
        return false;
    }
  }
  if (branch != IrRejectedBranch::IrInvariantRejected || isCapabilityKind(kind)) { return false; }

  constexpr IrFailureKind common[] = {
      IrFailureKind::InputRevisionMismatch,  IrFailureKind::MissingRequiredFact,
      IrFailureKind::AdditionalFact,         IrFailureKind::InvalidFact,
      IrFailureKind::CanonicalCodecMismatch,
  };
  constexpr IrFailureKind targetCommon[] = {
      IrFailureKind::InputRevisionMismatch,
      IrFailureKind::MissingRequiredFact,
      IrFailureKind::InvalidFact,
      IrFailureKind::CanonicalCodecMismatch,
  };
  if (phase == IrFailurePhase::TargetSelection) { return isOneOf(kind, targetCommon); }
  if (phase == IrFailurePhase::FeatureBoundaryVerification) { return isOneOf(kind, common); }
  if (isOneOf(kind, common)) { return true; }

  switch (phase) {
    case IrFailurePhase::CheckedModuleAssembly:
      return false;
    case IrFailurePhase::HirConstruction:
    case IrFailurePhase::HirVerification:
      return kind == IrFailureKind::InvalidControlFlow || kind == IrFailureKind::UnresolvedDispatch;
    case IrFailurePhase::MirConstruction:
    case IrFailurePhase::BuiltMirVerification:
      return kind == IrFailureKind::InvalidControlFlow || kind == IrFailureKind::InvalidPlace ||
             kind == IrFailureKind::UnresolvedDispatch;
    case IrFailurePhase::OwnershipProofValidation:
      return kind == IrFailureKind::InvalidPlace || kind == IrFailureKind::InvalidOwnershipProof;
    case IrFailurePhase::CleanupElaboration:
      return kind == IrFailureKind::InvalidControlFlow || kind == IrFailureKind::InvalidPlace ||
             kind == IrFailureKind::InvalidOwnershipProof || kind == IrFailureKind::InvalidCleanup;
    case IrFailurePhase::CoroutineElaboration:
    case IrFailurePhase::ExecutableMirVerification:
      return kind == IrFailureKind::InvalidControlFlow || kind == IrFailureKind::InvalidPlace ||
             kind == IrFailureKind::InvalidOwnershipProof ||
             kind == IrFailureKind::InvalidCleanup || kind == IrFailureKind::InvalidCoroutineState;
    case IrFailurePhase::Monomorphization:
      return kind == IrFailureKind::UnresolvedDispatch;
    case IrFailurePhase::LirLowering:
      return kind == IrFailureKind::InvalidSsa || kind == IrFailureKind::MissingTargetLayout ||
             kind == IrFailureKind::InvalidAbi || kind == IrFailureKind::UnresolvedDispatch;
    case IrFailurePhase::LirVerification:
      return kind == IrFailureKind::InvalidSsa || kind == IrFailureKind::MissingTargetLayout ||
             kind == IrFailureKind::InvalidAbi;
    case IrFailurePhase::LlvmTranslation:
      return kind == IrFailureKind::InvalidSsa || kind == IrFailureKind::MissingTargetLayout ||
             kind == IrFailureKind::InvalidAbi || kind == IrFailureKind::BackendTranslationRejected;
    case IrFailurePhase::ObjectEmission:
      return kind == IrFailureKind::InvalidAbi || kind == IrFailureKind::BackendTranslationRejected;
    case IrFailurePhase::TargetSelection:
    case IrFailurePhase::FeatureBoundaryVerification:
      return false;
  }
  return false;
}

bool siteIs(zc::Maybe<IrFailureSiteKind> site, IrFailureSiteKind expected) noexcept {
  ZC_IF_SOME(value, site) { return value == expected; }
  return false;
}

bool siteIsNone(zc::Maybe<IrFailureSiteKind> site) noexcept { return site == zc::none; }

bool legalOwnerSite(IrFailurePhase phase, IrFailureOwnerKind owner,
                    zc::Maybe<IrFailureSiteKind> site) noexcept {
  switch (phase) {
    case IrFailurePhase::CheckedModuleAssembly:
      return owner == IrFailureOwnerKind::Module &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::FrontendHandoff));
    case IrFailurePhase::HirConstruction:
      if (owner == IrFailureOwnerKind::Module) {
        return siteIsNone(site) || siteIs(site, IrFailureSiteKind::FrontendHandoff);
      }
      return owner == IrFailureOwnerKind::Definition &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::FrontendHandoff) ||
              siteIs(site, IrFailureSiteKind::Hir));
    case IrFailurePhase::HirVerification:
      if (owner == IrFailureOwnerKind::Module) { return siteIsNone(site); }
      return owner == IrFailureOwnerKind::Definition &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::Hir));
    case IrFailurePhase::MirConstruction:
      return owner == IrFailureOwnerKind::Definition &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::Hir) ||
              siteIs(site, IrFailureSiteKind::Mir));
    case IrFailurePhase::BuiltMirVerification:
    case IrFailurePhase::OwnershipProofValidation:
    case IrFailurePhase::CleanupElaboration:
    case IrFailurePhase::CoroutineElaboration:
    case IrFailurePhase::ExecutableMirVerification:
      return owner == IrFailureOwnerKind::Definition &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::Mir));
    case IrFailurePhase::Monomorphization:
      return owner == IrFailureOwnerKind::Instance &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::Hir) ||
              siteIs(site, IrFailureSiteKind::Mir));
    case IrFailurePhase::TargetSelection:
      return owner == IrFailureOwnerKind::Session && siteIsNone(site);
    case IrFailurePhase::FeatureBoundaryVerification:
      return (owner == IrFailureOwnerKind::Module || owner == IrFailureOwnerKind::Definition) &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::FrontendHandoff));
    case IrFailurePhase::LirLowering:
      return owner == IrFailureOwnerKind::Instance &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::Mir) ||
              siteIs(site, IrFailureSiteKind::Lir));
    case IrFailurePhase::LirVerification:
      return owner == IrFailureOwnerKind::Instance &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::Lir));
    case IrFailurePhase::LlvmTranslation:
      return owner == IrFailureOwnerKind::Instance &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::Lir) ||
              siteIs(site, IrFailureSiteKind::Backend));
    case IrFailurePhase::ObjectEmission:
      return (owner == IrFailureOwnerKind::Session || owner == IrFailureOwnerKind::Instance) &&
             (siteIsNone(site) || siteIs(site, IrFailureSiteKind::Backend));
  }
  return false;
}

bool legalDetail(IrRejectedBranch branch, IrFailureKind kind, IrFailureDetailKind detail) noexcept {
  if (branch == IrRejectedBranch::IrInvariantRejected) {
    return detail == IrFailureDetailKind::None;
  }
  if (kind == IrFailureKind::RecursiveInstantiation) {
    return detail == IrFailureDetailKind::InstantiationCycle;
  }
  if (kind == IrFailureKind::InstantiationBudgetExceeded) {
    return detail == IrFailureDetailKind::InstantiationBudget;
  }
  return detail == IrFailureDetailKind::None;
}

template <typename T>
zc::Vector<T> cloneScalarVector(zc::ArrayPtr<const T> values) {
  zc::Vector<T> result(values.size());
  for (const auto value : values) result.add(value);
  return result;
}

checker::checked::CheckedNodeKey cloneCheckedNode(const checker::checked::CheckedNodeKey& value) {
  return {value.syntaxKind, value.schemaPreorder, value.sourceSpan.clone()};
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

int compareOptionalBytes(zc::Maybe<zc::ArrayPtr<const uint8_t>> left,
                         zc::Maybe<zc::ArrayPtr<const uint8_t>> right) noexcept {
  if (left == zc::none) { return right == zc::none ? 0 : -1; }
  if (right == zc::none) { return 1; }
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) { return compareBytes(leftValue, rightValue); }
  }
  ZC_UNREACHABLE
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> encodeIdentityRange(
    zc::Maybe<const identity::UnbrandedSourceRange&> range, zc::Array<uint8_t>& storage) {
  ZC_IF_SOME(value, range) {
    storage = value.encode();
    zc::ArrayPtr<const uint8_t> bytes = storage.asPtr();
    return bytes;
  }
  return zc::none;
}

struct CanonicalKeyState final {
  explicit CanonicalKeyState(const IrFailureIdentityResolver& resolver) noexcept
      : identities(resolver) {}

  const IrFailureIdentityResolver& identities;
  zc::Maybe<identity::IdentityInvariant> identityFailure;
};

void encodeExpandedResult(identity::CanonicalEncoder& encoder, ExpandedIrIdentityResult&& result,
                          CanonicalKeyState& state) {
  if (state.identityFailure != zc::none) { return; }
  if (result.is<ExpandedIrIdentityValue>()) {
    encoder.encodeByteString(result.get<ExpandedIrIdentityValue>().value.bytes());
    return;
  }
  state.identityFailure = zc::mv(result.get<RejectedIrIdentityValue>().failure);
}

void encodeInstance(identity::CanonicalEncoder& encoder, InstanceId instance,
                    CanonicalKeyState& state) {
  encodeExpandedResult(encoder, state.identities.expand(instance), state);
}

void encodeOwner(identity::CanonicalEncoder& encoder, const IrFailureOwner& owner,
                 CanonicalKeyState& state) {
  encoder.encodeUint8(static_cast<uint8_t>(owner.kind()));
  switch (owner.kind()) {
    case IrFailureOwnerKind::Session:
      ZC_IF_SOME(context, owner.sessionContext()) { encoder.encodeDigest(context.digest()); }
      return;
    case IrFailureOwnerKind::Module:
      ZC_IF_SOME(module, owner.moduleId()) {
        encodeExpandedResult(encoder, state.identities.expand(module), state);
      }
      return;
    case IrFailureOwnerKind::Definition:
      ZC_IF_SOME(definition, owner.definitionId()) {
        encodeExpandedResult(encoder, state.identities.expand(definition), state);
      }
      return;
    case IrFailureOwnerKind::Instance:
      ZC_IF_SOME(instance, owner.instanceId()) { encodeInstance(encoder, instance, state); }
      return;
  }
}

void encodeSite(identity::CanonicalEncoder& encoder, zc::Maybe<const IrFailureSite&> site,
                CanonicalKeyState& state) {
  if (site == zc::none) {
    encoder.encodeNone();
    return;
  }
  encoder.encodeSome();
  ZC_IF_SOME(value, site) {
    encoder.encodeUint8(static_cast<uint8_t>(value.kind()));
    switch (value.kind()) {
      case IrFailureSiteKind::FrontendHandoff: {
        const auto& node = value.frontendHandoffValue().checkedNode;
        encoder.encodeUint32(node.syntaxKind);
        encoder.encodeUint32(node.schemaPreorder);
        node.sourceSpan.encode(encoder);
        return;
      }
      case IrFailureSiteKind::Hir: {
        const auto& hir = value.hirValue();
        encodeExpandedResult(encoder, state.identities.expand(hir.owner), state);
        encoder.encodeUint32(hir.node.ordinal());
        return;
      }
      case IrFailureSiteKind::Mir: {
        const auto& mir = value.mirValue();
        encodeExpandedResult(encoder, state.identities.expand(mir.owner), state);
        encoder.encodeUint32(mir.block.ordinal());
        ZC_IF_SOME(statement, mir.statement) {
          encoder.encodeSome();
          encoder.encodeUint32(statement);
        } else {
          encoder.encodeNone();
        }
        return;
      }
      case IrFailureSiteKind::Lir: {
        const auto& lir = value.lirValue();
        encodeInstance(encoder, lir.instance, state);
        encoder.encodeUint32(lir.block.ordinal());
        ZC_IF_SOME(instruction, lir.instruction) {
          encoder.encodeSome();
          encoder.encodeUint32(instruction);
        } else {
          encoder.encodeNone();
        }
        return;
      }
      case IrFailureSiteKind::Backend: {
        const auto& backend = value.backendValue();
        ZC_IF_SOME(instance, backend.instance) {
          encoder.encodeSome();
          encodeInstance(encoder, instance, state);
        } else {
          encoder.encodeNone();
        }
        encoder.encodeUint8(static_cast<uint8_t>(backend.operation));
        return;
      }
    }
  }
}

void encodeDetail(identity::CanonicalEncoder& encoder, const IrFailureDetail& detail,
                  CanonicalKeyState& state) {
  encoder.encodeUint8(static_cast<uint8_t>(detail.kind()));
  if (detail.kind() == IrFailureDetailKind::None) { return; }
  if (detail.kind() == IrFailureDetailKind::InstantiationCycle) {
    const auto& cycle = detail.cycleValue();
    encodeInstance(encoder, cycle.root, state);
    encoder.encodeSequenceSize(cycle.expansionChain.size());
    for (const auto& instance : cycle.expansionChain) encodeInstance(encoder, instance, state);
    return;
  }
  const auto& budget = detail.budgetValue();
  encodeInstance(encoder, budget.root, state);
  encoder.encodeSequenceSize(budget.expansionChain.size());
  for (const auto& instance : budget.expansionChain) encodeInstance(encoder, instance, state);
  encoder.encodeUint64(budget.requestedInstanceCount);
  encoder.encodeUint64(budget.requestedSubstitutionNodeCount);
  encoder.encodeUint64(budget.instanceLimit);
  encoder.encodeUint64(budget.substitutionNodeLimit);
}

void encodeSourceSpan(identity::CanonicalEncoder& encoder,
                      zc::Maybe<const identity::SourceSpan&> sourceSpan) {
  ZC_IF_SOME(span, sourceSpan) {
    encoder.encodeSome();
    span.encode(encoder);
  } else {
    encoder.encodeNone();
  }
}

void encodeFieldPath(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const uint32_t> path) {
  encoder.encodeSequenceSize(path.size());
  for (const auto field : path) encoder.encodeUint32(field);
}

struct CanonicalKeyResult final {
  zc::Maybe<zc::Array<uint8_t>> key;
  zc::Maybe<identity::IdentityInvariant> identityFailure;
};

CanonicalKeyResult buildCanonicalKey(const IrFailureDescriptor& descriptor,
                                     const IrFailureIdentityResolver& identities) {
  identity::CanonicalEncoder encoder;
  CanonicalKeyState state(identities);
  if (descriptor.branch() == IrRejectedBranch::IrInvariantRejected) {
    encoder.encodeUint8(static_cast<uint8_t>(descriptor.phase()));
    encoder.encodeUint8(static_cast<uint8_t>(descriptor.kind()));
    encodeOwner(encoder, descriptor.owner(), state);
    encodeSite(encoder, descriptor.site(), state);
    encodeDetail(encoder, descriptor.detail(), state);
  } else {
    encodeOwner(encoder, descriptor.owner(), state);
    encoder.encodeUint8(static_cast<uint8_t>(descriptor.kind()));
    encodeDetail(encoder, descriptor.detail(), state);
  }
  encodeSourceSpan(encoder, descriptor.sourceSpan());
  encodeFieldPath(encoder, descriptor.structuralFieldPath());
  encoder.encodeUint32(descriptor.traversalOrdinal());
  if (state.identityFailure != zc::none) { return {zc::none, zc::mv(state.identityFailure)}; }
  zc::Maybe<zc::Array<uint8_t>> key = encoder.finish();
  return {zc::mv(key), zc::none};
}

bool matchesOwnerSiteIdentity(const IrFailureDescriptor& descriptor) noexcept {
  if (descriptor.site() == zc::none) { return true; }
  ZC_IF_SOME(site, descriptor.site()) {
    if (!site.isStructurallyValid()) { return false; }
    if (site.kind() == IrFailureSiteKind::Hir) {
      if (descriptor.owner().kind() == IrFailureOwnerKind::Definition) {
        ZC_IF_SOME(owner, descriptor.owner().definitionId()) {
          return owner == site.hirValue().owner;
        }
      }
      if (descriptor.owner().kind() == IrFailureOwnerKind::Instance) {
        ZC_IF_SOME(owner, descriptor.owner().instanceId()) {
          return owner.definition() == site.hirValue().owner;
        }
      }
    }
    if (site.kind() == IrFailureSiteKind::Mir) {
      if (descriptor.owner().kind() == IrFailureOwnerKind::Definition) {
        ZC_IF_SOME(owner, descriptor.owner().definitionId()) {
          return owner == site.mirValue().owner;
        }
      }
      if (descriptor.owner().kind() == IrFailureOwnerKind::Instance) {
        ZC_IF_SOME(owner, descriptor.owner().instanceId()) {
          return owner.definition() == site.mirValue().owner;
        }
      }
    }
    if (site.kind() == IrFailureSiteKind::Lir) {
      ZC_IF_SOME(owner, descriptor.owner().instanceId()) {
        return owner == site.lirValue().instance;
      }
    }
    if (site.kind() == IrFailureSiteKind::Backend) {
      const auto& backend = site.backendValue();
      if (descriptor.owner().kind() == IrFailureOwnerKind::Session) {
        return backend.instance == zc::none;
      }
      ZC_IF_SOME(owner, descriptor.owner().instanceId()) {
        ZC_IF_SOME(instance, backend.instance) { return owner == instance; }
      }
      return false;
    }
    return site.kind() == IrFailureSiteKind::FrontendHandoff;
  }
  return false;
}

zc::Vector<uint32_t> cloneFieldPath(zc::ArrayPtr<const uint32_t> path) {
  return cloneScalarVector<uint32_t>(path);
}

}  // namespace

IrFailureOwner::IrFailureOwner(SessionFailureOwner&& value) noexcept : value(zc::mv(value)) {}
IrFailureOwner::IrFailureOwner(ModuleFailureOwner value) noexcept : value(value) {}
IrFailureOwner::IrFailureOwner(DefinitionFailureOwner value) noexcept : value(value) {}
IrFailureOwner::IrFailureOwner(InstanceFailureOwner value) noexcept : value(value) {}

IrFailureOwner IrFailureOwner::session(identity::ContextFingerprint&& context) noexcept {
  return IrFailureOwner(SessionFailureOwner{zc::mv(context)});
}
IrFailureOwner IrFailureOwner::module(identity::ModuleId module) noexcept {
  return IrFailureOwner(ModuleFailureOwner{module});
}
IrFailureOwner IrFailureOwner::definition(identity::DefId definition) noexcept {
  return IrFailureOwner(DefinitionFailureOwner{definition});
}
IrFailureOwner IrFailureOwner::instance(InstanceId instance) noexcept {
  return IrFailureOwner(InstanceFailureOwner{instance});
}

IrFailureOwner IrFailureOwner::clone() const {
  switch (kind()) {
    case IrFailureOwnerKind::Session:
      return IrFailureOwner::session(value.get<SessionFailureOwner>().context.clone());
    case IrFailureOwnerKind::Module:
      return IrFailureOwner::module(value.get<ModuleFailureOwner>().module);
    case IrFailureOwnerKind::Definition:
      return IrFailureOwner::definition(value.get<DefinitionFailureOwner>().definition);
    case IrFailureOwnerKind::Instance:
      return IrFailureOwner::instance(value.get<InstanceFailureOwner>().instance);
  }
  ZC_UNREACHABLE
}

IrFailureOwnerKind IrFailureOwner::kind() const noexcept {
  if (value.is<SessionFailureOwner>()) { return IrFailureOwnerKind::Session; }
  if (value.is<ModuleFailureOwner>()) { return IrFailureOwnerKind::Module; }
  if (value.is<DefinitionFailureOwner>()) { return IrFailureOwnerKind::Definition; }
  return IrFailureOwnerKind::Instance;
}

zc::Maybe<const identity::ContextFingerprint&> IrFailureOwner::sessionContext() const {
  if (value.is<SessionFailureOwner>()) { return value.get<SessionFailureOwner>().context; }
  return zc::none;
}
zc::Maybe<identity::ModuleId> IrFailureOwner::moduleId() const noexcept {
  if (value.is<ModuleFailureOwner>()) { return value.get<ModuleFailureOwner>().module; }
  return zc::none;
}
zc::Maybe<identity::DefId> IrFailureOwner::definitionId() const noexcept {
  if (value.is<DefinitionFailureOwner>()) { return value.get<DefinitionFailureOwner>().definition; }
  return zc::none;
}
zc::Maybe<InstanceId> IrFailureOwner::instanceId() const noexcept {
  if (value.is<InstanceFailureOwner>()) { return value.get<InstanceFailureOwner>().instance; }
  return zc::none;
}
bool IrFailureOwner::isStructurallyValid() const noexcept {
  switch (kind()) {
    case IrFailureOwnerKind::Session:
      return true;
    case IrFailureOwnerKind::Module:
      return value.get<ModuleFailureOwner>().module.isValid();
    case IrFailureOwnerKind::Definition:
      return value.get<DefinitionFailureOwner>().definition.isValid();
    case IrFailureOwnerKind::Instance:
      return value.get<InstanceFailureOwner>().instance.isValid();
  }
  return false;
}

IrFailureSite::IrFailureSite(FrontendHandoffFailureSite&& value) noexcept : value(zc::mv(value)) {}
IrFailureSite::IrFailureSite(HirFailureSite value) noexcept : value(value) {}
IrFailureSite::IrFailureSite(MirFailureSite&& value) noexcept : value(zc::mv(value)) {}
IrFailureSite::IrFailureSite(LirFailureSite&& value) noexcept : value(zc::mv(value)) {}
IrFailureSite::IrFailureSite(BackendFailureSite&& value) noexcept : value(zc::mv(value)) {}

IrFailureSite IrFailureSite::frontendHandoff(
    checker::checked::CheckedNodeKey&& checkedNode) noexcept {
  return IrFailureSite(FrontendHandoffFailureSite{zc::mv(checkedNode)});
}
IrFailureSite IrFailureSite::hir(identity::DefId owner, hir::HirNodeId node) noexcept {
  return IrFailureSite(HirFailureSite{owner, node});
}
IrFailureSite IrFailureSite::mir(identity::DefId owner, mir::MirBlockId block,
                                 zc::Maybe<uint32_t> statement) noexcept {
  return IrFailureSite(MirFailureSite{owner, block, zc::mv(statement)});
}
IrFailureSite IrFailureSite::lir(InstanceId instance, lir::LirBlockId block,
                                 zc::Maybe<uint32_t> instruction) noexcept {
  return IrFailureSite(LirFailureSite{instance, block, zc::mv(instruction)});
}
IrFailureSite IrFailureSite::backend(zc::Maybe<InstanceId> instance,
                                     BackendOperation operation) noexcept {
  return IrFailureSite(BackendFailureSite{zc::mv(instance), operation});
}

IrFailureSite IrFailureSite::clone() const {
  switch (kind()) {
    case IrFailureSiteKind::FrontendHandoff:
      return frontendHandoff(cloneCheckedNode(frontendHandoffValue().checkedNode));
    case IrFailureSiteKind::Hir:
      return hir(hirValue().owner, hirValue().node);
    case IrFailureSiteKind::Mir:
      return mir(mirValue().owner, mirValue().block, mirValue().statement);
    case IrFailureSiteKind::Lir:
      return lir(lirValue().instance, lirValue().block, lirValue().instruction);
    case IrFailureSiteKind::Backend:
      return backend(backendValue().instance, backendValue().operation);
  }
  ZC_UNREACHABLE
}

IrFailureSiteKind IrFailureSite::kind() const noexcept {
  if (value.is<FrontendHandoffFailureSite>()) { return IrFailureSiteKind::FrontendHandoff; }
  if (value.is<HirFailureSite>()) { return IrFailureSiteKind::Hir; }
  if (value.is<MirFailureSite>()) { return IrFailureSiteKind::Mir; }
  if (value.is<LirFailureSite>()) { return IrFailureSiteKind::Lir; }
  return IrFailureSiteKind::Backend;
}

bool IrFailureSite::isStructurallyValid() const noexcept {
  switch (kind()) {
    case IrFailureSiteKind::FrontendHandoff:
      return true;
    case IrFailureSiteKind::Hir:
      return hirValue().node.isValid();
    case IrFailureSiteKind::Mir:
      return mirValue().block.isValid();
    case IrFailureSiteKind::Lir:
      return lirValue().block.isValid();
    case IrFailureSiteKind::Backend:
      return enumInRange(backendValue().operation, BackendOperation::TranslateType,
                         BackendOperation::EmitObject);
  }
  return false;
}

const FrontendHandoffFailureSite& IrFailureSite::frontendHandoffValue() const {
  return value.get<FrontendHandoffFailureSite>();
}
const HirFailureSite& IrFailureSite::hirValue() const { return value.get<HirFailureSite>(); }
const MirFailureSite& IrFailureSite::mirValue() const { return value.get<MirFailureSite>(); }
const LirFailureSite& IrFailureSite::lirValue() const { return value.get<LirFailureSite>(); }
const BackendFailureSite& IrFailureSite::backendValue() const {
  return value.get<BackendFailureSite>();
}

IrFailureDetail::IrFailureDetail(NoIrFailureDetail value) noexcept : value(value) {}
IrFailureDetail::IrFailureDetail(InstantiationCycleFailureDetail&& value) noexcept
    : value(zc::mv(value)) {}
IrFailureDetail::IrFailureDetail(InstantiationBudgetFailureDetail&& value) noexcept
    : value(zc::mv(value)) {}

IrFailureDetail IrFailureDetail::none() noexcept { return IrFailureDetail(NoIrFailureDetail{}); }
zc::Maybe<IrFailureDetail> IrFailureDetail::instantiationCycle(
    InstanceId root, zc::Vector<InstanceId>&& expansionChain) noexcept {
  if (!root.isValid() || expansionChain.empty()) { return zc::none; }
  for (const auto& instance : expansionChain) {
    if (!instance.isValid()) { return zc::none; }
  }
  return IrFailureDetail(InstantiationCycleFailureDetail{root, zc::mv(expansionChain)});
}
zc::Maybe<IrFailureDetail> IrFailureDetail::instantiationBudget(
    InstanceId root, zc::Vector<InstanceId>&& expansionChain, uint64_t requestedInstanceCount,
    uint64_t requestedSubstitutionNodeCount, uint64_t instanceLimit,
    uint64_t substitutionNodeLimit) noexcept {
  if (!root.isValid() || expansionChain.empty() || instanceLimit == 0 ||
      substitutionNodeLimit == 0) {
    return zc::none;
  }
  for (const auto& instance : expansionChain) {
    if (!instance.isValid()) { return zc::none; }
  }
  return IrFailureDetail(InstantiationBudgetFailureDetail{
      root, zc::mv(expansionChain), requestedInstanceCount, requestedSubstitutionNodeCount,
      instanceLimit, substitutionNodeLimit});
}

IrFailureDetail IrFailureDetail::clone() const {
  if (kind() == IrFailureDetailKind::None) { return none(); }
  if (kind() == IrFailureDetailKind::InstantiationCycle) {
    auto chain = cloneScalarVector<InstanceId>(cycleValue().expansionChain.asPtr());
    ZC_IF_SOME(result, instantiationCycle(cycleValue().root, zc::mv(chain))) {
      return zc::mv(result);
    }
  } else {
    const auto& budget = budgetValue();
    auto chain = cloneScalarVector<InstanceId>(budget.expansionChain.asPtr());
    ZC_IF_SOME(result,
               instantiationBudget(budget.root, zc::mv(chain), budget.requestedInstanceCount,
                                   budget.requestedSubstitutionNodeCount, budget.instanceLimit,
                                   budget.substitutionNodeLimit)) {
      return zc::mv(result);
    }
  }
  ZC_IREQUIRE(false, "validated IR failure detail could not be cloned");
  ZC_UNREACHABLE
}

IrFailureDetailKind IrFailureDetail::kind() const noexcept {
  if (value.is<NoIrFailureDetail>()) { return IrFailureDetailKind::None; }
  if (value.is<InstantiationCycleFailureDetail>()) {
    return IrFailureDetailKind::InstantiationCycle;
  }
  return IrFailureDetailKind::InstantiationBudget;
}
bool IrFailureDetail::isStructurallyValid() const noexcept {
  if (kind() == IrFailureDetailKind::None) { return true; }
  if (kind() == IrFailureDetailKind::InstantiationCycle) {
    return cycleValue().root.isValid() && !cycleValue().expansionChain.empty();
  }
  return budgetValue().root.isValid() && !budgetValue().expansionChain.empty() &&
         budgetValue().instanceLimit != 0 && budgetValue().substitutionNodeLimit != 0;
}
const InstantiationCycleFailureDetail& IrFailureDetail::cycleValue() const {
  return value.get<InstantiationCycleFailureDetail>();
}
const InstantiationBudgetFailureDetail& IrFailureDetail::budgetValue() const {
  return value.get<InstantiationBudgetFailureDetail>();
}

bool isLegalIrFailureShape(const IrFailureDescriptorShape& shape) noexcept {
  if (!enumInRange(shape.branch, IrRejectedBranch::CapabilityRejected,
                   IrRejectedBranch::IrInvariantRejected) ||
      !enumInRange(shape.phase, IrFailurePhase::CheckedModuleAssembly,
                   IrFailurePhase::FeatureBoundaryVerification) ||
      !enumInRange(shape.kind, IrFailureKind::InputRevisionMismatch,
                   IrFailureKind::CanonicalCodecMismatch) ||
      !enumInRange(shape.owner, IrFailureOwnerKind::Session, IrFailureOwnerKind::Instance) ||
      !enumInRange(shape.detail, IrFailureDetailKind::None,
                   IrFailureDetailKind::InstantiationBudget)) {
    return false;
  }
  ZC_IF_SOME(site, shape.site) {
    if (!enumInRange(site, IrFailureSiteKind::FrontendHandoff, IrFailureSiteKind::Backend)) {
      return false;
    }
  }
  return legalKind(shape.branch, shape.phase, shape.kind) &&
         legalOwnerSite(shape.phase, shape.owner, shape.site) &&
         legalDetail(shape.branch, shape.kind, shape.detail);
}

IrFailureDescriptor::IrFailureDescriptor(IrRejectedBranch branch, IrFailurePhase phase,
                                         IrFailureKind kind, IrFailureOwner&& owner,
                                         zc::Maybe<IrFailureSite>&& site, IrFailureDetail&& detail,
                                         zc::Maybe<identity::SourceSpan>&& sourceSpan,
                                         zc::Vector<uint32_t>&& structuralFieldPath,
                                         uint32_t traversalOrdinal) noexcept
    : branchValue(branch),
      phaseValue(phase),
      kindValue(kind),
      ownerValue(zc::mv(owner)),
      siteValue(zc::mv(site)),
      detailValue(zc::mv(detail)),
      sourceSpanValue(zc::mv(sourceSpan)),
      structuralFieldPathValue(zc::mv(structuralFieldPath)),
      traversalOrdinalValue(traversalOrdinal) {}

IrFailureDescriptor IrFailureDescriptor::decoded(IrRejectedBranch branch, IrFailurePhase phase,
                                                 IrFailureKind kind, IrFailureOwner&& owner,
                                                 zc::Maybe<IrFailureSite>&& site,
                                                 IrFailureDetail&& detail,
                                                 zc::Maybe<identity::SourceSpan>&& sourceSpan,
                                                 zc::Vector<uint32_t>&& structuralFieldPath,
                                                 uint32_t traversalOrdinal) noexcept {
  return IrFailureDescriptor(branch, phase, kind, zc::mv(owner), zc::mv(site), zc::mv(detail),
                             zc::mv(sourceSpan), zc::mv(structuralFieldPath), traversalOrdinal);
}

IrFailureDescriptorShape IrFailureDescriptor::shape() const noexcept {
  zc::Maybe<IrFailureSiteKind> site;
  ZC_IF_SOME(value, siteValue) { site = value.kind(); }
  return {branchValue, phaseValue, kindValue, ownerValue.kind(), site, detailValue.kind()};
}
IrRejectedBranch IrFailureDescriptor::branch() const noexcept { return branchValue; }
IrFailurePhase IrFailureDescriptor::phase() const noexcept { return phaseValue; }
IrFailureKind IrFailureDescriptor::kind() const noexcept { return kindValue; }
const IrFailureOwner& IrFailureDescriptor::owner() const noexcept { return ownerValue; }
zc::Maybe<const IrFailureSite&> IrFailureDescriptor::site() const {
  ZC_IF_SOME(value, siteValue) { return value; }
  return zc::none;
}
const IrFailureDetail& IrFailureDescriptor::detail() const noexcept { return detailValue; }
zc::Maybe<const identity::SourceSpan&> IrFailureDescriptor::sourceSpan() const {
  ZC_IF_SOME(value, sourceSpanValue) { return value; }
  return zc::none;
}
zc::ArrayPtr<const uint32_t> IrFailureDescriptor::structuralFieldPath() const noexcept {
  return structuralFieldPathValue.asPtr();
}
uint32_t IrFailureDescriptor::traversalOrdinal() const noexcept { return traversalOrdinalValue; }

ExpandedIrIdentity::ExpandedIrIdentity(zc::Array<uint8_t>&& bytes) noexcept
    : value(zc::mv(bytes)) {}
zc::Maybe<ExpandedIrIdentity> ExpandedIrIdentity::from(zc::Array<uint8_t>&& bytes) noexcept {
  if (bytes.size() == 0) { return zc::none; }
  return ExpandedIrIdentity(zc::mv(bytes));
}
ExpandedIrIdentity ExpandedIrIdentity::clone() const {
  return ExpandedIrIdentity(zc::heapArray(value.asPtr()));
}
zc::ArrayPtr<const uint8_t> ExpandedIrIdentity::bytes() const noexcept { return value.asPtr(); }

IrFailureFact::IrFailureFact(IrFailureKind kind, IrFailurePhase phase, IrFailureOwner&& owner,
                             zc::Maybe<IrFailureSite>&& site, IrFailureDetail&& detail,
                             zc::Maybe<identity::SourceSpan>&& sourceSpan,
                             zc::Vector<uint32_t>&& structuralFieldPath, uint32_t traversalOrdinal,
                             zc::Array<uint8_t>&& canonicalSortKey) noexcept
    : kindValue(kind),
      phaseValue(phase),
      ownerValue(zc::mv(owner)),
      siteValue(zc::mv(site)),
      detailValue(zc::mv(detail)),
      sourceSpanValue(zc::mv(sourceSpan)),
      structuralFieldPathValue(zc::mv(structuralFieldPath)),
      traversalOrdinalValue(traversalOrdinal),
      canonicalSortKeyValue(zc::mv(canonicalSortKey)) {}

IrFailureFact IrFailureFact::clone() const {
  zc::Maybe<IrFailureSite> site;
  ZC_IF_SOME(value, siteValue) { site = value.clone(); }
  zc::Maybe<identity::SourceSpan> span;
  ZC_IF_SOME(value, sourceSpanValue) { span = value.clone(); }
  return IrFailureFact(kindValue, phaseValue, ownerValue.clone(), zc::mv(site), detailValue.clone(),
                       zc::mv(span), cloneFieldPath(structuralFieldPathValue.asPtr()),
                       traversalOrdinalValue, zc::heapArray(canonicalSortKeyValue.asPtr()));
}
IrRejectedBranch IrFailureFact::branch() const noexcept {
  return isCapabilityKind(kindValue) ? IrRejectedBranch::CapabilityRejected
                                     : IrRejectedBranch::IrInvariantRejected;
}
IrFailureKind IrFailureFact::kind() const noexcept { return kindValue; }
IrFailurePhase IrFailureFact::phase() const noexcept { return phaseValue; }
const IrFailureOwner& IrFailureFact::owner() const noexcept { return ownerValue; }
zc::Maybe<const IrFailureSite&> IrFailureFact::site() const {
  ZC_IF_SOME(value, siteValue) { return value; }
  return zc::none;
}
const IrFailureDetail& IrFailureFact::detail() const noexcept { return detailValue; }
zc::Maybe<const identity::SourceSpan&> IrFailureFact::sourceSpan() const {
  ZC_IF_SOME(value, sourceSpanValue) { return value; }
  return zc::none;
}
zc::ArrayPtr<const uint32_t> IrFailureFact::structuralFieldPath() const noexcept {
  return structuralFieldPathValue.asPtr();
}
uint32_t IrFailureFact::traversalOrdinal() const noexcept { return traversalOrdinalValue; }
zc::ArrayPtr<const uint8_t> IrFailureFact::canonicalSortKey() const noexcept {
  return canonicalSortKeyValue.asPtr();
}

IrFailureFallbackContext::IrFailureFallbackContext(IrFailurePhase phase,
                                                   IrFailureOwner&& owner) noexcept
    : phaseValue(phase), ownerValue(zc::mv(owner)) {}
zc::Maybe<IrFailureFallbackContext> IrFailureFallbackContext::from(
    IrFailurePhase phase, IrFailureOwner&& owner) noexcept {
  const IrFailureDescriptorShape fallback{IrRejectedBranch::IrInvariantRejected,
                                          phase,
                                          IrFailureKind::InvalidFact,
                                          owner.kind(),
                                          zc::none,
                                          IrFailureDetailKind::None};
  if (!isLegalIrFailureShape(fallback)) { return zc::none; }
  return IrFailureFallbackContext(phase, zc::mv(owner));
}

IrFailureAdmissionResult IrFailureFactory::admit(IrFailureDescriptor&& descriptor,
                                                 const IrFailureFallbackContext& fallback,
                                                 const IrFailureIdentityResolver& identities) {
  const auto rejectedShape = descriptor.shape();
  const bool descriptorLegal = isLegalIrFailureShape(rejectedShape) &&
                               descriptor.detailValue.isStructurallyValid() &&
                               matchesOwnerSiteIdentity(descriptor);
  if (descriptorLegal) {
    auto key = buildCanonicalKey(descriptor, identities);
    ZC_IF_SOME(identityFailure, key.identityFailure) {
      return IdentityRejectedIrFailureDescriptor{zc::mv(identityFailure)};
    }
    ZC_IF_SOME(bytes, key.key) {
      return AcceptedIrFailureDescriptor{IrFailureFact(
          descriptor.kindValue, descriptor.phaseValue, zc::mv(descriptor.ownerValue),
          zc::mv(descriptor.siteValue), zc::mv(descriptor.detailValue),
          zc::mv(descriptor.sourceSpanValue), zc::mv(descriptor.structuralFieldPathValue),
          descriptor.traversalOrdinalValue, zc::mv(bytes))};
    }
    ZC_UNREACHABLE
  }

  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  auto fallbackDescriptor = IrFailureDescriptor::decoded(
      IrRejectedBranch::IrInvariantRejected, fallback.phaseValue, IrFailureKind::InvalidFact,
      fallback.ownerValue.clone(), zc::mv(noSite), IrFailureDetail::none(), zc::mv(noSpan),
      cloneFieldPath(descriptor.structuralFieldPathValue.asPtr()),
      descriptor.traversalOrdinalValue);
  ZC_IREQUIRE(isLegalIrFailureShape(fallbackDescriptor.shape()),
              "descriptor fallback context must be a legal invariant row");
  auto key = buildCanonicalKey(fallbackDescriptor, identities);
  ZC_IF_SOME(identityFailure, key.identityFailure) {
    return IdentityRejectedIrFailureDescriptor{zc::mv(identityFailure)};
  }
  ZC_IF_SOME(bytes, key.key) {
    return FallbackIrFailureDescriptor{
        IrFailureFact(fallbackDescriptor.kindValue, fallbackDescriptor.phaseValue,
                      zc::mv(fallbackDescriptor.ownerValue), zc::mv(fallbackDescriptor.siteValue),
                      zc::mv(fallbackDescriptor.detailValue),
                      zc::mv(fallbackDescriptor.sourceSpanValue),
                      zc::mv(fallbackDescriptor.structuralFieldPathValue),
                      fallbackDescriptor.traversalOrdinalValue, zc::mv(bytes)),
        rejectedShape};
  }
  ZC_UNREACHABLE
}

IrVerificationFailure::IrVerificationFailure(IdentityIrVerificationFailure&& value) noexcept
    : value(zc::mv(value)) {}
IrVerificationFailure::IrVerificationFailure(StructuredIrVerificationFailure&& value) noexcept
    : value(zc::mv(value)) {}
IrVerificationFailure IrVerificationFailure::identity(identity::IdentityInvariant&& fact) noexcept {
  return IrVerificationFailure(IdentityIrVerificationFailure{zc::mv(fact)});
}
IrVerificationFailure IrVerificationFailure::ir(IrFailureFact&& fact) noexcept {
  return IrVerificationFailure(StructuredIrVerificationFailure{zc::mv(fact)});
}
IrVerificationFailure IrVerificationFailure::clone() const {
  if (isIdentity()) { return identity(value.get<IdentityIrVerificationFailure>().fact.clone()); }
  return ir(value.get<StructuredIrVerificationFailure>().fact.clone());
}
bool IrVerificationFailure::isIdentity() const noexcept {
  return value.is<IdentityIrVerificationFailure>();
}
const identity::IdentityInvariant& IrVerificationFailure::identityFact() const {
  return value.get<IdentityIrVerificationFailure>().fact;
}
const IrFailureFact& IrVerificationFailure::irFact() const {
  return value.get<StructuredIrVerificationFailure>().fact;
}

bool IrFailureCanonicalOrdering::less(const IrFailureFact& left,
                                      const IrFailureFact& right) noexcept {
  return compareBytes(left.canonicalSortKey(), right.canonicalSortKey()) < 0;
}

bool IdentityInvariantCanonicalOrdering::less(const identity::IdentityInvariant& left,
                                              const identity::IdentityInvariant& right) {
  if (left.phase() != right.phase()) { return left.phase() < right.phase(); }
  if (left.kind() != right.kind()) { return left.kind() < right.kind(); }
  const int structural =
      compareOptionalBytes(left.structuralInputKey(), right.structuralInputKey());
  if (structural != 0) { return structural < 0; }
  zc::Array<uint8_t> leftRange;
  zc::Array<uint8_t> rightRange;
  const int range = compareOptionalBytes(encodeIdentityRange(left.diagnosticRange(), leftRange),
                                         encodeIdentityRange(right.diagnosticRange(), rightRange));
  if (range != 0) { return range < 0; }
  if (left.apiSite() != right.apiSite()) { return left.apiSite() < right.apiSite(); }
  return left.inputTraversalOrdinal() < right.inputTraversalOrdinal();
}

bool IrVerificationFailureCanonicalOrdering::less(const IrVerificationFailure& left,
                                                  const IrVerificationFailure& right) {
  if (left.isIdentity() != right.isIdentity()) { return left.isIdentity(); }
  if (left.isIdentity()) {
    return IdentityInvariantCanonicalOrdering::less(left.identityFact(), right.identityFact());
  }
  return IrFailureCanonicalOrdering::less(left.irFact(), right.irFact());
}

}  // namespace zomlang::compiler::ir
