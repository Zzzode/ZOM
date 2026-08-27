// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "compiler/checker/inference/inference-recovery-context.h"
#include "compiler/identity/brand.h"
#include "compiler/identity/semantic/type-id.h"

namespace zomlang::compiler::checker::inference {

class InferenceContext;

/// \brief Private-construction handle issued by exactly one inference context.
struct TypeVarTag final {
private:
  ZC_NODISCARD static constexpr identity::StoreHandle<TypeVarTag> issue(
      identity::SemanticContextBrand context, identity::RegistryBrand issuer,
      uint32_t slot) noexcept {
    return identity::StoreHandle<TypeVarTag>(context, issuer, slot);
  }

  ZC_NODISCARD static constexpr uint32_t slot(identity::StoreHandle<TypeVarTag> handle) noexcept {
    return handle.slot;
  }

  friend class InferenceContext;
};

using TypeVarId = identity::StoreHandle<TypeVarTag>;

/// \brief Canonical variable issuance position inside one inference owner.
struct InferenceVariableOrdinal final {
  uint32_t schemaPreorder;
  uint32_t localOrdinal;
};

/// \brief Closed inference-lifecycle and equality-solver rejection algebra.
enum class InferenceContextInvariant : uint8_t {
  InvalidToken = 0x01,
  TokenConsumed = 0x02,
  VariableSpaceExhausted = 0x03,
  InvalidVariableOrdinal = 0x04,
  ContextClosed = 0x05,
  ForeignVariable = 0x06,
  UnknownVariable = 0x07,
  ForeignSemanticType = 0x08,
  ForeignRecovery = 0x09,
  DuplicateConstraintOrdinal = 0x0a,
  ConstraintSpaceExhausted = 0x0b,
  PendingWork = 0x0c,
  UnresolvedVariable = 0x0d,
  OccursCheckFailed = 0x0e,
  SolverStateInvalid = 0x0f
};

/// \brief Structured invariant rejection without presentation text.
struct InferenceContextRejected final {
  InferenceContextInvariant invariant;
  uint32_t ordinal;
};

/// \brief Testable issuance bounds; production admits every non-wrapping slot.
struct InferenceContextBudget final {
  uint32_t variables = UINT32_MAX;
  uint32_t constraints = UINT32_MAX;
};

/// \brief Move-only, single-consumption construction authority.
class InferenceContextToken final {
public:
  using IssueResult = zc::OneOf<InferenceContextToken, InferenceContextRejected>;

  /// \brief Claims the sole inference core owned by one recovery context.
  ZC_NODISCARD static IssueResult issue(InferenceRecoveryContext& recoveryContext);

  InferenceContextToken(InferenceContextToken&& other) noexcept;
  InferenceContextToken& operator=(InferenceContextToken&& other) noexcept;
  ZC_DISALLOW_COPY(InferenceContextToken);

  /// \brief Returns true until a context consumes this authority.
  ZC_NODISCARD bool isValid() const noexcept;

private:
  InferenceContextToken(identity::SemanticContextBrand context,
                        identity::RegistryBrand issuer) noexcept;

  identity::SemanticContextBrand context;
  identity::RegistryBrand issuer;
  zc::Maybe<InferenceRecoveryContext&> recoveryContext;
  bool available = false;

  friend class InferenceContext;
};

/// \brief A closed semantic type participating in local inference.
struct KnownInferenceType final {
  identity::SemanticTypeId type;
};

/// \brief A mutable context-local type variable participating in inference.
struct VariableInferenceType final {
  TypeVarId variable;
};

/// \brief A source-recovery identity suppressing one inference cascade.
struct RecoveryInferenceType final {
  checked::TypeErrorId error;
};

/// \brief Closed RFC 0005 inference-type algebra.
class InferenceType final {
public:
  ZC_NODISCARD static InferenceType known(identity::SemanticTypeId type) noexcept;
  ZC_NODISCARD static InferenceType variable(TypeVarId variable) noexcept;
  ZC_NODISCARD static InferenceType recovery(checked::TypeErrorId error) noexcept;

  InferenceType(InferenceType&&) noexcept = default;
  InferenceType& operator=(InferenceType&&) noexcept = default;
  ZC_DISALLOW_COPY(InferenceType);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  explicit InferenceType(KnownInferenceType type) noexcept : value(type) {}
  explicit InferenceType(VariableInferenceType type) noexcept : value(type) {}
  explicit InferenceType(RecoveryInferenceType type) noexcept : value(type) {}

  zc::OneOf<KnownInferenceType, VariableInferenceType, RecoveryInferenceType> value;
};

struct InferenceConstraintAccepted final {};
struct InferenceSolveComplete final {};
struct InferenceContextClosed final {};

struct MaterializedInferenceType final {
  identity::SemanticTypeId type;
};

struct SourceRejectedInferenceType final {
  checked::TypeErrorId error;
};

using InferenceContextCreationResult =
    zc::OneOf<zc::Own<InferenceContext>, InferenceContextRejected>;
using TypeVariableIssueResult = zc::OneOf<TypeVarId, InferenceContextRejected>;
using InferenceConstraintResult = zc::OneOf<InferenceConstraintAccepted, InferenceContextRejected>;
using InferenceSolveResult = zc::OneOf<InferenceSolveComplete, InferenceContextRejected>;
using InferenceRepresentativeResult = zc::OneOf<TypeVarId, InferenceContextRejected>;
using InferenceMaterializationResult =
    zc::OneOf<MaterializedInferenceType, SourceRejectedInferenceType, InferenceContextRejected>;
using InferenceContextFinishResult = zc::OneOf<InferenceContextClosed, InferenceContextRejected>;

/// \brief Function-local deterministic equality inference context.
class InferenceContext final {
public:
  /// \brief Consumes exactly one construction token.
  ZC_NODISCARD static InferenceContextCreationResult create(InferenceContextToken&& token,
                                                            InferenceContextBudget budget = {});

  ~InferenceContext() noexcept(false);
  InferenceContext(InferenceContext&&) noexcept;
  InferenceContext& operator=(InferenceContext&&) noexcept;
  ZC_DISALLOW_COPY(InferenceContext);

  /// \brief Issues a variable in strictly increasing schema-preorder order.
  ZC_NODISCARD TypeVariableIssueResult issueVariable(InferenceVariableOrdinal ordinal);

  /// \brief Queues one equality under a unique deterministic ordinal.
  ZC_NODISCARD InferenceConstraintResult addEquality(uint32_t ordinal, InferenceType&& left,
                                                     InferenceType&& right);

  /// \brief Drains equality constraints in ordinal FIFO order.
  ZC_NODISCARD InferenceSolveResult solve();

  /// \brief Returns the smallest-slot representative of one same-issuer set.
  ZC_NODISCARD InferenceRepresentativeResult representative(TypeVarId variable) const;

  /// \brief Materializes a solved type or preserves its source-recovery identity.
  ZC_NODISCARD InferenceMaterializationResult materialize(const InferenceType& type) const;

  /// \brief Sole closing operation; rejects pending work or unresolved variables.
  ZC_NODISCARD InferenceContextFinishResult finish();

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::RegistryBrand issuer() const noexcept;

private:
  struct Impl;
  explicit InferenceContext(zc::Own<Impl>&& impl) noexcept;

  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::checker::inference
