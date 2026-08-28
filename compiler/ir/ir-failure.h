// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "compiler/checker/inference/checked-facts.h"
#include "compiler/hir/hir-node-id.h"
#include "compiler/identity/identity-invariant.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "compiler/ir/ir-identity.h"

namespace zomlang::compiler::ir {

enum class IrFailureOwnerKind : uint8_t {
  Session = 0x01,
  Module = 0x02,
  Definition = 0x03,
  Instance = 0x04,
};

enum class IrFailureSiteKind : uint8_t {
  FrontendHandoff = 0x01,
  Hir = 0x02,
  Mir = 0x03,
  Lir = 0x04,
  Backend = 0x05,
};

enum class IrFailurePhase : uint8_t {
  CheckedModuleAssembly = 0x01,
  HirConstruction = 0x02,
  HirVerification = 0x03,
  MirConstruction = 0x04,
  BuiltMirVerification = 0x05,
  OwnershipProofValidation = 0x06,
  CleanupElaboration = 0x07,
  CoroutineElaboration = 0x08,
  ExecutableMirVerification = 0x09,
  Monomorphization = 0x0a,
  TargetSelection = 0x0b,
  LirLowering = 0x0c,
  LirVerification = 0x0d,
  LlvmTranslation = 0x0e,
  ObjectEmission = 0x0f,
  FeatureBoundaryVerification = 0x10,
  // RFC 0043 "Linker And Publication Failure Algebra": three closed post-object
  // phases at the next free tags. They add no new IrFailureKind; they reuse the
  // existing kinds bound to these new phases.
  LinkPlanConstruction = 0x11,
  LinkerInvocation = 0x12,
  ExecutablePublication = 0x13,
};

enum class IrFailureKind : uint8_t {
  InputRevisionMismatch = 0x01,
  MissingRequiredFact = 0x02,
  AdditionalFact = 0x03,
  InvalidFact = 0x04,
  InvalidControlFlow = 0x05,
  InvalidPlace = 0x06,
  InvalidOwnershipProof = 0x07,
  InvalidCleanup = 0x08,
  InvalidCoroutineState = 0x09,
  InvalidSsa = 0x0a,
  MissingTargetLayout = 0x0b,
  InvalidAbi = 0x0c,
  UnresolvedDispatch = 0x0d,
  UnsupportedTargetCapability = 0x0e,
  BackendTranslationRejected = 0x0f,
  RecursiveInstantiation = 0x10,
  InstantiationBudgetExceeded = 0x11,
  OutputCreationFailed = 0x12,
  CanonicalCodecMismatch = 0x13,
};

enum class IrFailureDetailKind : uint8_t {
  None = 0x01,
  InstantiationCycle = 0x02,
  InstantiationBudget = 0x03,
};

enum class BackendOperation : uint8_t {
  TranslateType = 0x01,
  DeclareFunction = 0x02,
  DefineFunction = 0x03,
  EmitCall = 0x04,
  EmitBranch = 0x05,
  EmitReturn = 0x06,
  EmitPanic = 0x07,
  EmitDebugInfo = 0x08,
  VerifyLlvm = 0x09,
  EmitObject = 0x0a,
  // RFC 0043 "Linker And Publication Failure Algebra": a linker subprocess
  // failure carries a Backend site like every other backend operation.
  InvokeLinker = 0x0b,
};

enum class IrRejectedBranch : uint8_t {
  CapabilityRejected = 0x01,
  IrInvariantRejected = 0x02,
};

struct SessionFailureOwner final {
  identity::ContextFingerprint context;
};

struct ModuleFailureOwner final {
  identity::ModuleId module;
};

struct DefinitionFailureOwner final {
  identity::DefId definition;
};

struct InstanceFailureOwner final {
  InstanceId instance;
};

/// \brief Closed owner algebra for every RFC 0010 failure fact.
class IrFailureOwner final {
public:
  IrFailureOwner(IrFailureOwner&&) noexcept = default;
  IrFailureOwner& operator=(IrFailureOwner&&) noexcept = default;
  ZC_DISALLOW_COPY(IrFailureOwner);

  ZC_NODISCARD static IrFailureOwner session(
      identity::ContextFingerprint&& context) noexcept;
  ZC_NODISCARD static IrFailureOwner module(identity::ModuleId module) noexcept;
  ZC_NODISCARD static IrFailureOwner definition(identity::DefId definition) noexcept;
  ZC_NODISCARD static IrFailureOwner instance(InstanceId instance) noexcept;

  ZC_NODISCARD IrFailureOwner clone() const;
  ZC_NODISCARD IrFailureOwnerKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::ContextFingerprint&> sessionContext() const;
  ZC_NODISCARD zc::Maybe<identity::ModuleId> moduleId() const noexcept;
  ZC_NODISCARD zc::Maybe<identity::DefId> definitionId() const noexcept;
  ZC_NODISCARD zc::Maybe<InstanceId> instanceId() const noexcept;
  ZC_NODISCARD bool isStructurallyValid() const noexcept;

private:
  explicit IrFailureOwner(SessionFailureOwner&& value) noexcept;
  explicit IrFailureOwner(ModuleFailureOwner value) noexcept;
  explicit IrFailureOwner(DefinitionFailureOwner value) noexcept;
  explicit IrFailureOwner(InstanceFailureOwner value) noexcept;

  zc::OneOf<SessionFailureOwner, ModuleFailureOwner, DefinitionFailureOwner, InstanceFailureOwner>
      value;
};

struct FrontendHandoffFailureSite final {
  checker::checked::CheckedNodeKey checkedNode;
};

struct HirFailureSite final {
  identity::DefId owner;
  hir::HirNodeId node;
};

struct MirFailureSite final {
  identity::DefId owner;
  mir::MirBlockId block;
  zc::Maybe<uint32_t> statement;
};

struct LirFailureSite final {
  InstanceId instance;
  lir::LirBlockId block;
  zc::Maybe<uint32_t> instruction;
};

struct BackendFailureSite final {
  zc::Maybe<InstanceId> instance;
  BackendOperation operation;
};

/// \brief Closed structural location algebra for an RFC 0010 failure.
class IrFailureSite final {
public:
  IrFailureSite(IrFailureSite&&) noexcept = default;
  IrFailureSite& operator=(IrFailureSite&&) noexcept = default;
  ZC_DISALLOW_COPY(IrFailureSite);

  ZC_NODISCARD static IrFailureSite frontendHandoff(
      checker::checked::CheckedNodeKey&& checkedNode) noexcept;
  ZC_NODISCARD static IrFailureSite hir(identity::DefId owner, hir::HirNodeId node) noexcept;
  ZC_NODISCARD static IrFailureSite mir(identity::DefId owner, mir::MirBlockId block,
                                        zc::Maybe<uint32_t> statement) noexcept;
  ZC_NODISCARD static IrFailureSite lir(InstanceId instance, lir::LirBlockId block,
                                        zc::Maybe<uint32_t> instruction) noexcept;
  ZC_NODISCARD static IrFailureSite backend(zc::Maybe<InstanceId> instance,
                                            BackendOperation operation) noexcept;

  ZC_NODISCARD IrFailureSite clone() const;
  ZC_NODISCARD IrFailureSiteKind kind() const noexcept;
  ZC_NODISCARD bool isStructurallyValid() const noexcept;
  ZC_NODISCARD const FrontendHandoffFailureSite& frontendHandoffValue() const;
  ZC_NODISCARD const HirFailureSite& hirValue() const;
  ZC_NODISCARD const MirFailureSite& mirValue() const;
  ZC_NODISCARD const LirFailureSite& lirValue() const;
  ZC_NODISCARD const BackendFailureSite& backendValue() const;

private:
  explicit IrFailureSite(FrontendHandoffFailureSite&& value) noexcept;
  explicit IrFailureSite(HirFailureSite value) noexcept;
  explicit IrFailureSite(MirFailureSite&& value) noexcept;
  explicit IrFailureSite(LirFailureSite&& value) noexcept;
  explicit IrFailureSite(BackendFailureSite&& value) noexcept;

  zc::OneOf<FrontendHandoffFailureSite, HirFailureSite, MirFailureSite, LirFailureSite,
            BackendFailureSite>
      value;
};

struct NoIrFailureDetail final {};

struct InstantiationCycleFailureDetail final {
  InstanceId root;
  zc::Vector<InstanceId> expansionChain;
};

struct InstantiationBudgetFailureDetail final {
  InstanceId root;
  zc::Vector<InstanceId> expansionChain;
  uint64_t requestedInstanceCount;
  uint64_t requestedSubstitutionNodeCount;
  uint64_t instanceLimit;
  uint64_t substitutionNodeLimit;
};

/// \brief Closed typed payload algebra for capability failures.
class IrFailureDetail final {
public:
  IrFailureDetail(IrFailureDetail&&) noexcept = default;
  IrFailureDetail& operator=(IrFailureDetail&&) noexcept = default;
  ZC_DISALLOW_COPY(IrFailureDetail);

  ZC_NODISCARD static IrFailureDetail none() noexcept;
  ZC_NODISCARD static zc::Maybe<IrFailureDetail> instantiationCycle(
      InstanceId root, zc::Vector<InstanceId>&& expansionChain) noexcept;
  ZC_NODISCARD static zc::Maybe<IrFailureDetail> instantiationBudget(
      InstanceId root, zc::Vector<InstanceId>&& expansionChain, uint64_t requestedInstanceCount,
      uint64_t requestedSubstitutionNodeCount, uint64_t instanceLimit,
      uint64_t substitutionNodeLimit) noexcept;

  ZC_NODISCARD IrFailureDetail clone() const;
  ZC_NODISCARD IrFailureDetailKind kind() const noexcept;
  ZC_NODISCARD bool isStructurallyValid() const noexcept;
  ZC_NODISCARD const InstantiationCycleFailureDetail& cycleValue() const;
  ZC_NODISCARD const InstantiationBudgetFailureDetail& budgetValue() const;

private:
  explicit IrFailureDetail(NoIrFailureDetail value) noexcept;
  explicit IrFailureDetail(InstantiationCycleFailureDetail&& value) noexcept;
  explicit IrFailureDetail(InstantiationBudgetFailureDetail&& value) noexcept;

  zc::OneOf<NoIrFailureDetail, InstantiationCycleFailureDetail, InstantiationBudgetFailureDetail>
      value;
};

/// \brief Closed matrix coordinates used by codecs and exhaustive verifier tests.
struct IrFailureDescriptorShape final {
  IrRejectedBranch branch;
  IrFailurePhase phase;
  IrFailureKind kind;
  IrFailureOwnerKind owner;
  zc::Maybe<IrFailureSiteKind> site;
  IrFailureDetailKind detail;
};

/// \brief Returns whether one descriptor shape is an exact legal RFC 0010 matrix row.
ZC_NODISCARD bool isLegalIrFailureShape(const IrFailureDescriptorShape& shape) noexcept;

/// \brief Decoded or phase-specific structured failure candidate before validation.
class IrFailureDescriptor final {
public:
  IrFailureDescriptor(IrFailureDescriptor&&) noexcept = default;
  IrFailureDescriptor& operator=(IrFailureDescriptor&&) noexcept = default;
  ZC_DISALLOW_COPY(IrFailureDescriptor);

  /// \brief Constructs a typed descriptor for validation after codec or builder assembly.
  ZC_NODISCARD static IrFailureDescriptor decoded(IrRejectedBranch branch, IrFailurePhase phase,
                                                  IrFailureKind kind, IrFailureOwner&& owner,
                                                  zc::Maybe<IrFailureSite>&& site,
                                                  IrFailureDetail&& detail,
                                                  zc::Maybe<identity::SourceSpan>&& sourceSpan,
                                                  zc::Vector<uint32_t>&& structuralFieldPath,
                                                  uint32_t traversalOrdinal) noexcept;

  ZC_NODISCARD IrFailureDescriptorShape shape() const noexcept;
  ZC_NODISCARD IrRejectedBranch branch() const noexcept;
  ZC_NODISCARD IrFailurePhase phase() const noexcept;
  ZC_NODISCARD IrFailureKind kind() const noexcept;
  ZC_NODISCARD const IrFailureOwner& owner() const noexcept;
  ZC_NODISCARD zc::Maybe<const IrFailureSite&> site() const;
  ZC_NODISCARD const IrFailureDetail& detail() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceSpan&> sourceSpan() const;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> structuralFieldPath() const noexcept;
  ZC_NODISCARD uint32_t traversalOrdinal() const noexcept;

private:
  IrFailureDescriptor(IrRejectedBranch branch, IrFailurePhase phase, IrFailureKind kind,
                      IrFailureOwner&& owner, zc::Maybe<IrFailureSite>&& site,
                      IrFailureDetail&& detail, zc::Maybe<identity::SourceSpan>&& sourceSpan,
                      zc::Vector<uint32_t>&& structuralFieldPath,
                      uint32_t traversalOrdinal) noexcept;

  IrRejectedBranch branchValue;
  IrFailurePhase phaseValue;
  IrFailureKind kindValue;
  IrFailureOwner ownerValue;
  zc::Maybe<IrFailureSite> siteValue;
  IrFailureDetail detailValue;
  zc::Maybe<identity::SourceSpan> sourceSpanValue;
  zc::Vector<uint32_t> structuralFieldPathValue;
  uint32_t traversalOrdinalValue;

  friend class IrFailureFactory;
};

/// \brief Canonical expanded structural identity supplied by identity-owning registries.
class ExpandedIrIdentity final {
public:
  ExpandedIrIdentity(ExpandedIrIdentity&&) noexcept = default;
  ExpandedIrIdentity& operator=(ExpandedIrIdentity&&) noexcept = default;
  ZC_DISALLOW_COPY(ExpandedIrIdentity);

  ZC_NODISCARD static zc::Maybe<ExpandedIrIdentity> from(zc::Array<uint8_t>&& bytes) noexcept;
  ZC_NODISCARD ExpandedIrIdentity clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept;

private:
  explicit ExpandedIrIdentity(zc::Array<uint8_t>&& bytes) noexcept;
  zc::Array<uint8_t> value;
};

struct ExpandedIrIdentityValue final {
  ExpandedIrIdentity value;
};

struct RejectedIrIdentityValue final {
  identity::IdentityInvariant failure;
};

using ExpandedIrIdentityResult = zc::OneOf<ExpandedIrIdentityValue, RejectedIrIdentityValue>;

/// \brief Read-only bridge from context handles to canonical structural identity bytes.
class IrFailureIdentityResolver {
public:
  virtual ~IrFailureIdentityResolver() noexcept(false) = default;

  ZC_NODISCARD virtual ExpandedIrIdentityResult expand(identity::ModuleId module) const = 0;
  ZC_NODISCARD virtual ExpandedIrIdentityResult expand(identity::DefId definition) const = 0;
  ZC_NODISCARD virtual ExpandedIrIdentityResult expand(InstanceId instance) const = 0;
};

/// \brief Validated immutable RFC 0010 failure fact with a complete canonical sort key.
class IrFailureFact final {
public:
  IrFailureFact(IrFailureFact&&) noexcept = default;
  IrFailureFact& operator=(IrFailureFact&&) noexcept = default;
  ZC_DISALLOW_COPY(IrFailureFact);

  ZC_NODISCARD IrFailureFact clone() const;
  ZC_NODISCARD IrRejectedBranch branch() const noexcept;
  ZC_NODISCARD IrFailureKind kind() const noexcept;
  ZC_NODISCARD IrFailurePhase phase() const noexcept;
  ZC_NODISCARD const IrFailureOwner& owner() const noexcept;
  ZC_NODISCARD zc::Maybe<const IrFailureSite&> site() const;
  ZC_NODISCARD const IrFailureDetail& detail() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceSpan&> sourceSpan() const;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> structuralFieldPath() const noexcept;
  ZC_NODISCARD uint32_t traversalOrdinal() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalSortKey() const noexcept;

private:
  IrFailureFact(IrFailureKind kind, IrFailurePhase phase, IrFailureOwner&& owner,
                zc::Maybe<IrFailureSite>&& site, IrFailureDetail&& detail,
                zc::Maybe<identity::SourceSpan>&& sourceSpan,
                zc::Vector<uint32_t>&& structuralFieldPath, uint32_t traversalOrdinal,
                zc::Array<uint8_t>&& canonicalSortKey) noexcept;

  IrFailureKind kindValue;
  IrFailurePhase phaseValue;
  IrFailureOwner ownerValue;
  zc::Maybe<IrFailureSite> siteValue;
  IrFailureDetail detailValue;
  zc::Maybe<identity::SourceSpan> sourceSpanValue;
  zc::Vector<uint32_t> structuralFieldPathValue;
  uint32_t traversalOrdinalValue;
  zc::Array<uint8_t> canonicalSortKeyValue;

  friend class IrFailureFactory;
};

struct AcceptedIrFailureDescriptor final {
  IrFailureFact fact;
};

struct FallbackIrFailureDescriptor final {
  IrFailureFact fact;
  IrFailureDescriptorShape rejectedShape;
};

struct IdentityRejectedIrFailureDescriptor final {
  identity::IdentityInvariant failure;
};

using IrFailureAdmissionResult = zc::OneOf<AcceptedIrFailureDescriptor, FallbackIrFailureDescriptor,
                                           IdentityRejectedIrFailureDescriptor>;

/// \brief Known operation context used for the one-step invalid-descriptor fallback.
class IrFailureFallbackContext final {
public:
  IrFailureFallbackContext(IrFailureFallbackContext&&) noexcept = default;
  IrFailureFallbackContext& operator=(IrFailureFallbackContext&&) noexcept = default;
  ZC_DISALLOW_COPY(IrFailureFallbackContext);

  ZC_NODISCARD static zc::Maybe<IrFailureFallbackContext> from(IrFailurePhase phase,
                                                               IrFailureOwner&& owner) noexcept;

private:
  IrFailureFallbackContext(IrFailurePhase phase, IrFailureOwner&& owner) noexcept;
  IrFailurePhase phaseValue;
  IrFailureOwner ownerValue;

  friend class IrFailureFactory;
};

/// \brief Sole admission point from typed descriptors to verified failure facts.
class IrFailureFactory final {
public:
  /// \brief Validates one descriptor or emits the known operation's non-recursive fallback.
  ZC_NODISCARD static IrFailureAdmissionResult admit(IrFailureDescriptor&& descriptor,
                                                     const IrFailureFallbackContext& fallback,
                                                     const IrFailureIdentityResolver& identities);
};

struct IdentityIrVerificationFailure final {
  identity::IdentityInvariant fact;
};

struct StructuredIrVerificationFailure final {
  IrFailureFact fact;
};

/// \brief Unified identity-or-IR invariant fact for deterministic aggregation.
class IrVerificationFailure final {
public:
  IrVerificationFailure(IrVerificationFailure&&) noexcept = default;
  IrVerificationFailure& operator=(IrVerificationFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(IrVerificationFailure);

  ZC_NODISCARD static IrVerificationFailure identity(identity::IdentityInvariant&& fact) noexcept;
  ZC_NODISCARD static IrVerificationFailure ir(IrFailureFact&& fact) noexcept;
  ZC_NODISCARD IrVerificationFailure clone() const;
  ZC_NODISCARD bool isIdentity() const noexcept;
  ZC_NODISCARD const identity::IdentityInvariant& identityFact() const;
  ZC_NODISCARD const IrFailureFact& irFact() const;

private:
  explicit IrVerificationFailure(IdentityIrVerificationFailure&& value) noexcept;
  explicit IrVerificationFailure(StructuredIrVerificationFailure&& value) noexcept;

  zc::OneOf<IdentityIrVerificationFailure, StructuredIrVerificationFailure> value;
};

struct IrFailureCanonicalOrdering final {
  ZC_NODISCARD static bool less(const IrFailureFact& left, const IrFailureFact& right) noexcept;
};

struct IdentityInvariantCanonicalOrdering final {
  ZC_NODISCARD static bool less(const identity::IdentityInvariant& left,
                                const identity::IdentityInvariant& right);
};

struct IrVerificationFailureCanonicalOrdering final {
  ZC_NODISCARD static bool less(const IrVerificationFailure& left,
                                const IrVerificationFailure& right);
};

/// \brief Move-only canonical non-empty sequence used by every rejected result branch.
template <typename Fact, typename Ordering>
class SortedNonEmptyFailureSequence final {
public:
  SortedNonEmptyFailureSequence(SortedNonEmptyFailureSequence&&) noexcept = default;
  SortedNonEmptyFailureSequence& operator=(SortedNonEmptyFailureSequence&&) noexcept = default;
  ZC_DISALLOW_COPY(SortedNonEmptyFailureSequence);

  /// \brief Sorts a non-empty sequence by its closed canonical ordering.
  ZC_NODISCARD static zc::Maybe<SortedNonEmptyFailureSequence> from(zc::Vector<Fact>&& facts) {
    if (facts.empty()) { return zc::none; }
    for (size_t index = 1; index < facts.size(); ++index) {
      auto current = zc::mv(facts[index]);
      size_t insertion = index;
      while (insertion != 0 && Ordering::less(current, facts[insertion - 1])) {
        facts[insertion] = zc::mv(facts[insertion - 1]);
        --insertion;
      }
      facts[insertion] = zc::mv(current);
    }
    return SortedNonEmptyFailureSequence(zc::mv(facts));
  }

  ZC_NODISCARD zc::ArrayPtr<const Fact> facts() const noexcept { return values.asPtr(); }

private:
  explicit SortedNonEmptyFailureSequence(zc::Vector<Fact>&& facts) noexcept
      : values(zc::mv(facts)) {}

  zc::Vector<Fact> values;
};

using SortedIrFailureFacts =
    SortedNonEmptyFailureSequence<IrFailureFact, IrFailureCanonicalOrdering>;
using SortedIdentityInvariantFacts =
    SortedNonEmptyFailureSequence<identity::IdentityInvariant, IdentityInvariantCanonicalOrdering>;
using SortedIrVerificationFailures =
    SortedNonEmptyFailureSequence<IrVerificationFailure, IrVerificationFailureCanonicalOrdering>;

/// \brief Canonically sorted non-empty capability failures only.
class SortedCapabilityFailureFacts final {
public:
  SortedCapabilityFailureFacts(SortedCapabilityFailureFacts&&) noexcept = default;
  SortedCapabilityFailureFacts& operator=(SortedCapabilityFailureFacts&&) noexcept = default;
  ZC_DISALLOW_COPY(SortedCapabilityFailureFacts);

  ZC_NODISCARD static zc::Maybe<SortedCapabilityFailureFacts> from(
      zc::Vector<IrFailureFact>&& facts) {
    for (const auto& fact : facts) {
      if (fact.branch() != IrRejectedBranch::CapabilityRejected) { return zc::none; }
    }
    auto sorted = SortedIrFailureFacts::from(zc::mv(facts));
    ZC_IF_SOME(values, sorted) { return SortedCapabilityFailureFacts(zc::mv(values)); }
    return zc::none;
  }

  ZC_NODISCARD zc::ArrayPtr<const IrFailureFact> facts() const noexcept { return values.facts(); }

private:
  explicit SortedCapabilityFailureFacts(SortedIrFailureFacts&& facts) noexcept
      : values(zc::mv(facts)) {}

  SortedIrFailureFacts values;
};

/// \brief Canonically sorted non-empty IR invariant failures only.
class SortedIrInvariantFailureFacts final {
public:
  SortedIrInvariantFailureFacts(SortedIrInvariantFailureFacts&&) noexcept = default;
  SortedIrInvariantFailureFacts& operator=(SortedIrInvariantFailureFacts&&) noexcept = default;
  ZC_DISALLOW_COPY(SortedIrInvariantFailureFacts);

  ZC_NODISCARD static zc::Maybe<SortedIrInvariantFailureFacts> from(
      zc::Vector<IrFailureFact>&& facts) {
    for (const auto& fact : facts) {
      if (fact.branch() != IrRejectedBranch::IrInvariantRejected) { return zc::none; }
    }
    auto sorted = SortedIrFailureFacts::from(zc::mv(facts));
    ZC_IF_SOME(values, sorted) { return SortedIrInvariantFailureFacts(zc::mv(values)); }
    return zc::none;
  }

  ZC_NODISCARD zc::ArrayPtr<const IrFailureFact> facts() const noexcept { return values.facts(); }

private:
  explicit SortedIrInvariantFailureFacts(SortedIrFailureFacts&& facts) noexcept
      : values(zc::mv(facts)) {}

  SortedIrFailureFacts values;
};

template <typename VerifiedValue>
struct VerifiedIrOperation final {
  VerifiedValue value;
};

struct CapabilityRejectedIrOperation final {
  SortedCapabilityFailureFacts failures;
};

struct IdentityInvariantRejectedIrOperation final {
  SortedIdentityInvariantFacts failures;
};

struct IrInvariantRejectedIrOperation final {
  SortedIrInvariantFailureFacts failures;
};

/// \brief Closed result algebra used by every IR builder, verifier, pass, and backend operation.
template <typename VerifiedValue>
class IrOperationResult final {
public:
  IrOperationResult(IrOperationResult&&) noexcept = default;
  IrOperationResult& operator=(IrOperationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(IrOperationResult);

  ZC_NODISCARD static IrOperationResult verified(VerifiedValue&& value) noexcept {
    return IrOperationResult(VerifiedIrOperation<VerifiedValue>{zc::mv(value)});
  }
  ZC_NODISCARD static IrOperationResult capabilityRejected(
      SortedCapabilityFailureFacts&& failures) noexcept {
    return IrOperationResult(CapabilityRejectedIrOperation{zc::mv(failures)});
  }
  ZC_NODISCARD static IrOperationResult identityInvariantRejected(
      SortedIdentityInvariantFacts&& failures) noexcept {
    return IrOperationResult(IdentityInvariantRejectedIrOperation{zc::mv(failures)});
  }
  ZC_NODISCARD static IrOperationResult irInvariantRejected(
      SortedIrInvariantFailureFacts&& failures) noexcept {
    return IrOperationResult(IrInvariantRejectedIrOperation{zc::mv(failures)});
  }

  ZC_NODISCARD bool isVerified() const noexcept {
    return value.template is<VerifiedIrOperation<VerifiedValue>>();
  }
  ZC_NODISCARD bool isCapabilityRejected() const noexcept {
    return value.template is<CapabilityRejectedIrOperation>();
  }
  ZC_NODISCARD bool isIdentityInvariantRejected() const noexcept {
    return value.template is<IdentityInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD bool isIrInvariantRejected() const noexcept {
    return value.template is<IrInvariantRejectedIrOperation>();
  }

  ZC_NODISCARD const VerifiedValue& verifiedValue() const {
    return value.template get<VerifiedIrOperation<VerifiedValue>>().value;
  }
  ZC_NODISCARD VerifiedValue&& takeVerified() && {
    return zc::mv(value.template get<VerifiedIrOperation<VerifiedValue>>().value);
  }
  ZC_NODISCARD const SortedCapabilityFailureFacts& capabilityFailures() const {
    return value.template get<CapabilityRejectedIrOperation>().failures;
  }
  ZC_NODISCARD const SortedIdentityInvariantFacts& identityFailures() const {
    return value.template get<IdentityInvariantRejectedIrOperation>().failures;
  }
  ZC_NODISCARD const SortedIrInvariantFailureFacts& invariantFailures() const {
    return value.template get<IrInvariantRejectedIrOperation>().failures;
  }
  ZC_NODISCARD SortedCapabilityFailureFacts&& takeCapabilityFailures() && {
    return zc::mv(value.template get<CapabilityRejectedIrOperation>().failures);
  }
  ZC_NODISCARD SortedIdentityInvariantFacts&& takeIdentityFailures() && {
    return zc::mv(value.template get<IdentityInvariantRejectedIrOperation>().failures);
  }
  ZC_NODISCARD SortedIrInvariantFailureFacts&& takeInvariantFailures() && {
    return zc::mv(value.template get<IrInvariantRejectedIrOperation>().failures);
  }

private:
  explicit IrOperationResult(VerifiedIrOperation<VerifiedValue>&& result) noexcept
      : value(zc::mv(result)) {}
  explicit IrOperationResult(CapabilityRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}
  explicit IrOperationResult(IdentityInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}
  explicit IrOperationResult(IrInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}

  zc::OneOf<VerifiedIrOperation<VerifiedValue>, CapabilityRejectedIrOperation,
            IdentityInvariantRejectedIrOperation, IrInvariantRejectedIrOperation>
      value;
};

template <typename SourceFailure, typename SourceFailureOrdering>
using SortedSourceFailureFacts =
    SortedNonEmptyFailureSequence<SourceFailure, SourceFailureOrdering>;

template <typename VerifiedValue>
struct VerifiedFeatureBoundary final {
  VerifiedValue value;
};

template <typename SourceFailure, typename SourceFailureOrdering>
struct SourceRejectedFeatureBoundary final {
  SortedSourceFailureFacts<SourceFailure, SourceFailureOrdering> failures;
};

/// \brief Canonical source-rejecting result extension for verified compiler boundaries.
template <typename VerifiedValue, typename SourceFailure, typename SourceFailureOrdering>
class FeatureBoundaryVerificationResult final {
public:
  using SourceFailures = SortedSourceFailureFacts<SourceFailure, SourceFailureOrdering>;

  FeatureBoundaryVerificationResult(FeatureBoundaryVerificationResult&&) noexcept = default;
  FeatureBoundaryVerificationResult& operator=(FeatureBoundaryVerificationResult&&) noexcept =
      default;
  ZC_DISALLOW_COPY(FeatureBoundaryVerificationResult);

  ZC_NODISCARD static FeatureBoundaryVerificationResult verified(VerifiedValue&& value) noexcept {
    return FeatureBoundaryVerificationResult(VerifiedFeatureBoundary<VerifiedValue>{zc::mv(value)});
  }
  ZC_NODISCARD static FeatureBoundaryVerificationResult sourceRejected(
      SourceFailures&& failures) noexcept {
    return FeatureBoundaryVerificationResult(
        SourceRejectedFeatureBoundary<SourceFailure, SourceFailureOrdering>{zc::mv(failures)});
  }
  ZC_NODISCARD static FeatureBoundaryVerificationResult identityInvariantRejected(
      SortedIdentityInvariantFacts&& failures) noexcept {
    return FeatureBoundaryVerificationResult(
        IdentityInvariantRejectedIrOperation{zc::mv(failures)});
  }
  ZC_NODISCARD static FeatureBoundaryVerificationResult irInvariantRejected(
      SortedIrInvariantFailureFacts&& failures) noexcept {
    return FeatureBoundaryVerificationResult(IrInvariantRejectedIrOperation{zc::mv(failures)});
  }

  ZC_NODISCARD bool isVerified() const noexcept {
    return value.template is<VerifiedFeatureBoundary<VerifiedValue>>();
  }
  ZC_NODISCARD bool isSourceRejected() const noexcept {
    return value.template is<SourceRejected>();
  }
  ZC_NODISCARD bool isIdentityInvariantRejected() const noexcept {
    return value.template is<IdentityInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD bool isIrInvariantRejected() const noexcept {
    return value.template is<IrInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD VerifiedValue&& takeVerified() && {
    return zc::mv(value.template get<VerifiedFeatureBoundary<VerifiedValue>>().value);
  }
  ZC_NODISCARD SourceFailures&& takeSourceFailures() && {
    return zc::mv(value.template get<SourceRejected>().failures);
  }
  ZC_NODISCARD SortedIdentityInvariantFacts&& takeIdentityFailures() && {
    return zc::mv(value.template get<IdentityInvariantRejectedIrOperation>().failures);
  }
  ZC_NODISCARD SortedIrInvariantFailureFacts&& takeInvariantFailures() && {
    return zc::mv(value.template get<IrInvariantRejectedIrOperation>().failures);
  }

private:
  using SourceRejected = SourceRejectedFeatureBoundary<SourceFailure, SourceFailureOrdering>;
  explicit FeatureBoundaryVerificationResult(
      VerifiedFeatureBoundary<VerifiedValue>&& result) noexcept
      : value(zc::mv(result)) {}
  explicit FeatureBoundaryVerificationResult(SourceRejected&& result) noexcept
      : value(zc::mv(result)) {}
  explicit FeatureBoundaryVerificationResult(IdentityInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}
  explicit FeatureBoundaryVerificationResult(IrInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}

  zc::OneOf<VerifiedFeatureBoundary<VerifiedValue>, SourceRejected,
            IdentityInvariantRejectedIrOperation, IrInvariantRejectedIrOperation>
      value;
};

}  // namespace zomlang::compiler::ir
