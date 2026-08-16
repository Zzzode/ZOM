// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/checker/body-checker.h"
#include "zomlang/compiler/checker/checked-facts-repository.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/identity/identity-invariant.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::driver::module_graph_query {
class CheckerBoundModuleView;
}

namespace zomlang::compiler::checker::dispatch {

struct DirectTarget final {
  identity::DefId callee;
};
struct ConcreteMethodTarget final {
  identity::DefId method;
};
struct ImplMethodTarget final {
  identity::ImplId impl;
  identity::DefId method;
};
struct WitnessMethodTarget final {
  identity::DefId witnessParameter;
  identity::DefId interface;
  identity::DefId method;
};
struct DynMethodTarget final {
  identity::DefId interface;
  identity::DefId method;
};
struct PrimitiveTarget final {
  PrimitiveOperation operation;
};

/// \brief Closed logical dispatch target algebra in RFC 0009 tag order.
class DispatchTarget final {
public:
  explicit DispatchTarget(DirectTarget value) noexcept : value(value) {}
  explicit DispatchTarget(ConcreteMethodTarget value) noexcept : value(value) {}
  explicit DispatchTarget(ImplMethodTarget value) noexcept : value(value) {}
  explicit DispatchTarget(WitnessMethodTarget value) noexcept : value(value) {}
  explicit DispatchTarget(DynMethodTarget value) noexcept : value(value) {}
  explicit DispatchTarget(PrimitiveTarget value) noexcept : value(value) {}
  DispatchTarget(DispatchTarget&&) noexcept = default;
  DispatchTarget& operator=(DispatchTarget&&) noexcept = default;
  ZC_DISALLOW_COPY(DispatchTarget);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<DirectTarget, ConcreteMethodTarget, ImplMethodTarget, WitnessMethodTarget,
            DynMethodTarget, PrimitiveTarget>
      value;
};

enum class DispatchReceiverRole : uint8_t {
  ExplicitFirstArgument = 0x01,
  ImplicitSelf = 0x02,
  OperatorLeftHandSide = 0x03,
  OperatorOperand = 0x04,
  IndexBase = 0x05
};

enum class OrderingRelation : uint8_t {
  Less = 0x01,
  LessEqual = 0x02,
  Greater = 0x03,
  GreaterEqual = 0x04
};

struct IdentityResultTransform final {};
struct BooleanNotResultTransform final {};
struct CompareOrderingResultTransform final {
  OrderingRelation relation;
};

/// \brief Closed successful-payload transform algebra in RFC 0009 tag order.
class DispatchResultTransform final {
public:
  explicit DispatchResultTransform(IdentityResultTransform value) noexcept : value(value) {}
  explicit DispatchResultTransform(BooleanNotResultTransform value) noexcept : value(value) {}
  explicit DispatchResultTransform(CompareOrderingResultTransform value) noexcept : value(value) {}
  DispatchResultTransform(DispatchResultTransform&&) noexcept = default;
  DispatchResultTransform& operator=(DispatchResultTransform&&) noexcept = default;
  ZC_DISALLOW_COPY(DispatchResultTransform);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<IdentityResultTransform, BooleanNotResultTransform, CompareOrderingResultTransform>
      value;
};

/// \brief One retained argument plan keyed by a canonical checked-node identity, never NodeId.
struct DispatchArgumentPlan final {
  checked::CheckedNodeKey sourceNode;
  identity::SemanticTypeId sourceType;
  identity::SemanticTypeId parameterType;
  zc::Maybe<checked::CoercionAdjustment> adjustment;
};

struct DispatchReceiverPlan final {
  DispatchReceiverRole role;
  checked::ReceiverMode passing;
  DispatchArgumentPlan value;
  checked::ReceiverAdjustment adjustment;
};

/// \brief Complete logical dispatch record retained after candidate verification.
struct DispatchFact final {
  DispatchTarget target;
  DispatchResultTransform resultTransform;
  zc::Maybe<DispatchReceiverPlan> receiver;
  zc::Vector<DispatchArgumentPlan> arguments;
  identity::SemanticTypeId successType;
  identity::SemanticTypeId resultType;
  zc::Maybe<checked::CanonicalSubstitutionId> substitutions;
  zc::Maybe<checked::WitnessArgumentsId> witnesses;
  zc::Maybe<identity::SemanticTypeId> raises;
  identity::SourceSpan sourceSpan;
};

/// \brief Candidate-only NodeId to stable checked-node projection.
struct DispatchNodeProjection final {
  ast::NodeId sourceNode;
  checked::CheckedNodeKey checkedNode;
};

enum class DispatchSiteKind : uint8_t {
  Call = 0x01,
  UnaryOperator = 0x02,
  BinaryOperator = 0x03,
  Index = 0x04,
  CompoundAssignment = 0x05,
  NullCoalescing = 0x06
};

/// \brief Generated exact call-like site requirement consumed by verification.
struct DispatchSiteRequirement final {
  ast::NodeId sourceNode;
  checked::CheckedNodeKey checkedNode;
  zc::Maybe<identity::DefId> owner;
  DispatchSiteKind siteKind;
  zc::Maybe<DispatchReceiverRole> receiverRole;
  zc::Maybe<PrimitiveOperation> operation;
  zc::Maybe<CompoundAssignmentOperation> compoundOperation;
};

/// \brief One independently encoded candidate record; NodeId is discarded on publication.
struct DispatchFactCandidateEntry final {
  ast::NodeId sourceNode;
  checked::CheckedNodeKey checkedNode;
  zc::Maybe<identity::DefId> owner;
  DispatchFact fact;
  zc::Array<uint8_t> canonicalRecord;
};

/// \brief Move-only untrusted dispatch fact candidate.
class DispatchFactsCandidate final {
public:
  DispatchFactsCandidate(identity::SemanticContextBrand semanticContext,
                         identity::ContextFingerprint&& contextFingerprint,
                         identity::ModuleId module,
                         const checked::CheckedFactsRevision& checkedFactsRevision,
                         zc::Vector<DispatchFactCandidateEntry>&& facts);
  ~DispatchFactsCandidate() noexcept(false);
  DispatchFactsCandidate(DispatchFactsCandidate&&) noexcept;
  DispatchFactsCandidate& operator=(DispatchFactsCandidate&&) noexcept;
  ZC_DISALLOW_COPY(DispatchFactsCandidate);

private:
  struct Impl;
  zc::Own<Impl> impl;
  friend class DispatchFactsVerifier;
};

/// \brief Domain-separated revision of one verified RFC 0009 publication.
class DispatchFactsRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<DispatchFactsRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const checked::CheckedFactsRevision& checkedFactsRevision,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> completeCanonicalRecords);
  /// \brief Pure codec entry point used by independent revision oracles.
  ZC_NODISCARD static zc::Maybe<DispatchFactsRevision> computeFramedDigest(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& checkedFactsRevision,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> completeCanonicalRecords);

private:
  explicit DispatchFactsRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest value;
};

enum class DispatchInvariantKind : uint8_t {
  InputMismatch = 0x01,
  MissingFact = 0x02,
  AdditionalFact = 0x03,
  InvalidFact = 0x04,
  CanonicalCodecMismatch = 0x05
};

enum class DispatchInvariantStage : uint8_t {
  Input = 0x01,
  Construction = 0x02,
  Verification = 0x03,
  Encoding = 0x04
};

struct DispatchInvariantFact final {
  DispatchInvariantKind kind;
  DispatchInvariantStage stage;
  identity::ModuleId module;
  zc::Maybe<identity::DefId> owner;
  zc::Maybe<checked::CheckedNodeKey> node;
  zc::Maybe<identity::SourceSpan> sourceSpan;
  zc::Vector<uint32_t> structuralFieldPath;
  zc::Maybe<identity::Sha256Digest> expectedCheckedRevision;
  zc::Maybe<identity::Sha256Digest> actualCheckedRevision;
  uint32_t traversalOrdinal;
};

/// \brief Closed identity-or-dispatch invariant failure algebra.
class DispatchVerificationFailure final {
public:
  explicit DispatchVerificationFailure(identity::IdentityInvariant&& value)
      : value(zc::mv(value)) {}
  explicit DispatchVerificationFailure(DispatchInvariantFact&& value) : value(zc::mv(value)) {}
  DispatchVerificationFailure(DispatchVerificationFailure&&) noexcept = default;
  DispatchVerificationFailure& operator=(DispatchVerificationFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(DispatchVerificationFailure);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<identity::IdentityInvariant, DispatchInvariantFact> value;
};

struct DispatchFactsInvariantRejected final {
  zc::Vector<DispatchVerificationFailure> failures;
};

/// \brief Verified AST-to-checked-node inventory for every dispatch-producing site.
class VerifiedDispatchSiteInventory final {
public:
  ~VerifiedDispatchSiteInventory() noexcept(false);
  VerifiedDispatchSiteInventory(VerifiedDispatchSiteInventory&&) noexcept;
  VerifiedDispatchSiteInventory& operator=(VerifiedDispatchSiteInventory&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedDispatchSiteInventory);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& semanticFingerprint() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceContentDigest() const noexcept;
  ZC_NODISCARD const binder::ParsedModuleReceipt& parsedModuleReceipt() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DispatchSiteRequirement> requirements() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DispatchNodeProjection> nodeProjections() const noexcept;

private:
  struct Impl;
  explicit VerifiedDispatchSiteInventory(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class DispatchSiteInventoryBuilder;
};

using DispatchSiteInventoryBuildResult =
    zc::OneOf<VerifiedDispatchSiteInventory, DispatchFactsInvariantRejected>;

/// \brief Generates the exact dispatch-site inventory from one verified body requirement set.
class DispatchSiteInventoryBuilder final {
public:
  ZC_NODISCARD static DispatchSiteInventoryBuildResult build(
      const driver::module_graph_query::CheckerBoundModuleView& boundModule,
      const body::VerifiedBodyFactRequirementInventory& bodyRequirements);
};

/// \brief One NodeId-free immutable published dispatch record.
struct VerifiedDispatchFact final {
  checked::CheckedNodeKey checkedNode;
  zc::Maybe<identity::DefId> owner;
  DispatchFact fact;
  zc::Array<uint8_t> canonicalRecord;
};

/// \brief Immutable verified dispatch capability bound to checked evidence lineage.
class VerifiedDispatchFacts final {
public:
  ~VerifiedDispatchFacts() noexcept(false);
  VerifiedDispatchFacts(VerifiedDispatchFacts&&) noexcept;
  VerifiedDispatchFacts& operator=(VerifiedDispatchFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedDispatchFacts);

  ZC_NODISCARD const DispatchFactsRevision& revision() const noexcept;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const checked::CheckedFactsRevision& checkedFactsRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedDispatchFact> facts() const noexcept;

private:
  struct Impl;
  explicit VerifiedDispatchFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class DispatchFactsVerifier;
};

using DispatchVerificationResult = zc::OneOf<VerifiedDispatchFacts, DispatchFactsInvariantRejected>;

/// \brief Exact checked-evidence and generated inventory authority for dispatch verification.
struct DispatchFactsVerificationInput final {
  const identity::ContextFingerprint& contextFingerprint;
  identity::ModuleId module;
  const identity::SourceFileKey& source;
  zc::ArrayPtr<const DispatchSiteRequirement> requirements;
  zc::ArrayPtr<const DispatchNodeProjection> nodeProjections;
  const checked::CheckedEvidenceLease& checkedLease;
  const checked::VerifiedCheckedFacts& checkedFacts;
  const CheckerIdentityAuthority& identities;
  const type::SemanticTypeStore& semanticTypes;
};

/// \brief Canonical encoder for one complete NodeId-free dispatch record.
class DispatchFactCanonicalCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encode(
      const checked::CheckedNodeKey& checkedNode, const DispatchFact& fact,
      const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes,
      const checked::VerifiedCheckedFacts& checkedFacts);
};

/// \brief Verifies exact dispatch coverage, canonical bytes, and checked-evidence lineage.
class DispatchFactsVerifier final {
public:
  ZC_NODISCARD static DispatchVerificationResult verify(
      DispatchFactsCandidate&& candidate, const DispatchFactsVerificationInput& input);
};

using DispatchFactsBuildResult = zc::OneOf<DispatchFactsCandidate, DispatchFactsInvariantRejected>;

/// \brief Copies total RFC 0005 selections into one untrusted RFC 0009 candidate.
class DispatchFactsBuilder final {
public:
  ZC_NODISCARD static DispatchFactsBuildResult build(
      const VerifiedDispatchSiteInventory& inventory,
      const identity::ContextFingerprint& contextFingerprint,
      const checked::CheckedEvidenceLease& checkedLease,
      const checked::VerifiedCheckedFacts& checkedFacts, const CheckerIdentityAuthority& identities,
      const type::SemanticTypeStore& semanticTypes);
};

}  // namespace zomlang::compiler::checker::dispatch
