// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/inference/checked-facts.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"

namespace zomlang::compiler::checker::inference {

/// \brief One declaration-signature dependency group that owns local inference.
struct SignatureGroupInferenceOwner final {
  identity::ModuleId module;
  zc::Vector<identity::DefId> members;
};

/// \brief One callable body that owns local inference.
struct CallableBodyInferenceOwner final {
  identity::DefId callable;
};

/// \brief One initializer that owns local inference.
struct InitializerInferenceOwner final {
  identity::DefId definition;
};

/// \brief Closed RFC 0005 inference-owner algebra.
class InferenceOwner final {
public:
  ZC_NODISCARD static InferenceOwner signatureGroup(identity::ModuleId module,
                                                    zc::Vector<identity::DefId>&& members);
  ZC_NODISCARD static InferenceOwner callableBody(identity::DefId callable) noexcept;
  ZC_NODISCARD static InferenceOwner initializer(identity::DefId definition) noexcept;
  InferenceOwner(InferenceOwner&&) noexcept = default;
  InferenceOwner& operator=(InferenceOwner&&) noexcept = default;
  ZC_DISALLOW_COPY(InferenceOwner);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  explicit InferenceOwner(SignatureGroupInferenceOwner&& owner) : value(zc::mv(owner)) {}
  explicit InferenceOwner(CallableBodyInferenceOwner owner) noexcept : value(owner) {}
  explicit InferenceOwner(InitializerInferenceOwner owner) noexcept : value(owner) {}

  zc::OneOf<SignatureGroupInferenceOwner, CallableBodyInferenceOwner, InitializerInferenceOwner>
      value;
};

/// \brief Root recovery classification in canonical RFC 0005 tag order.
enum class RecoveryClass : uint8_t {
  TypeMismatch = 0x01,
  InvalidOperation = 0x02,
  InvalidTypeExpression = 0x03,
  FailedObligation = 0x04,
  FailedProjection = 0x05,
  FailedInference = 0x06
};

/// \brief Closed misuse classification for the inference-recovery lifecycle.
enum class InferenceRecoveryInvariant : uint8_t {
  InvalidContext = 0x01,
  RegistryIssueFailed = 0x02,
  InvalidOwner = 0x03,
  InvalidRoot = 0x04,
  DuplicateRootOrdinal = 0x05,
  ErrorIdSpaceExhausted = 0x06,
  ContextClosed = 0x07,
  ForeignRecovery = 0x08,
  UnknownRecovery = 0x09,
  InvalidJoin = 0x0a,
  DuplicateJoinParent = 0x0b,
  CanonicalLedgerRejected = 0x0c,
  UnclosedInferenceContext = 0x0d
};

/// \brief Testable issuance bound; production permits every representable non-wrapping slot.
struct InferenceRecoveryIssueBudget final {
  uint32_t errorIds = UINT32_MAX;
};

/// \brief One closed Checker invariant rejection from recovery lifecycle validation.
struct InferenceRecoveryRejected final {
  InferenceRecoveryInvariant invariant;
  signature::CheckerVerificationFailure failure;
};

/// \brief No source recovery was issued before the context closed.
struct InferenceRecoverySolved final {};

/// \brief Canonical read-only recovery ledger transferred out of a closed context.
struct InferenceRecoveryRecovered final {
  checked::FrozenRecoveryLedger ledger;
};

class InferenceRecoveryContext;
class InferenceContextToken;

using InferenceRecoveryCreationResult =
    zc::OneOf<zc::Own<InferenceRecoveryContext>, InferenceRecoveryRejected>;
using TypeErrorIssueResult = zc::OneOf<checked::TypeErrorId, InferenceRecoveryRejected>;
using InferenceRecoveryFinishResult =
    zc::OneOf<InferenceRecoverySolved, InferenceRecoveryRecovered, InferenceRecoveryRejected>;

/// \brief Function-local issuer and canonical freezer for RFC 0005 recovery identities.
class InferenceRecoveryContext final {
public:
  /// \brief Creates one context with a fresh registry brand from the semantic context issuer.
  ZC_NODISCARD static InferenceRecoveryCreationResult create(
      const CheckerIdentityAuthority& identities,
      const identity::RegistryBrandIssuer& registryBrands, const identity::SourceFileKey& source,
      InferenceOwner&& owner, InferenceRecoveryIssueBudget budget = {});

  ~InferenceRecoveryContext() noexcept(false);
  InferenceRecoveryContext(InferenceRecoveryContext&&) noexcept;
  InferenceRecoveryContext& operator=(InferenceRecoveryContext&&) noexcept;
  ZC_DISALLOW_COPY(InferenceRecoveryContext);

  /// \brief Issues one unique root recovery identity in source traversal order.
  ZC_NODISCARD TypeErrorIssueResult
  issueRoot(const checked::CheckerEmitterOrdinal& rootFailureOrdinal, ast::NodeId rootNode,
            const identity::SourceSpan& rootSpan, RecoveryClass recoveryClass);

  /// \brief Validates and reuses exactly one recovery identity without allocating.
  ZC_NODISCARD TypeErrorIssueResult reuse(checked::TypeErrorId recovery);

  /// \brief Records one deterministic multi-root join and returns its selected recovery identity.
  ZC_NODISCARD TypeErrorIssueResult join(ast::NodeId parentNode,
                                         const checked::CheckedNodeKey& parent,
                                         zc::ArrayPtr<const checked::TypeErrorId> inputs);

  /// \brief Closes this context exactly once and transfers its canonical immutable ledger.
  ZC_NODISCARD InferenceRecoveryFinishResult finish();

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::RegistryBrand issuer() const noexcept;

private:
  ZC_NODISCARD bool claimInferenceContext() noexcept;
  ZC_NODISCARD bool completeInferenceContext() noexcept;

  struct Impl;
  explicit InferenceRecoveryContext(zc::Own<Impl>&& impl) noexcept;

  zc::Own<Impl> impl;

  friend class InferenceContextToken;
  friend class InferenceContext;
};

}  // namespace zomlang::compiler::checker::inference
