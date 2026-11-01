// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/function.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "compiler/checker/inference/checked-facts.h"
#include "compiler/identity/handle.h"
#include "compiler/ir/ir-identity.h"
#include "compiler/mir/built-mir.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::mono {

/// \brief Read-only evidence and template access used by the fixpoint core.
///
/// Production wiring supplies repository-backed records and post-elaboration
/// templates. Component tests supply a hand-built provider so the fixpoint,
/// cycle guard, budgets, and edge predicates are exercisable without forging a
/// sealed session artifact.
class MonoEvidenceProvider {
public:
  virtual ~MonoEvidenceProvider() noexcept(false) = default;

  /// \brief Returns the post-elaboration MIR template for a generic definition.
  ZC_NODISCARD virtual zc::Maybe<const mir::MirFunction&> templateFor(
      identity::DefId definition) const = 0;
  /// \brief Returns the ordered generic parameter keys, or none for non-generic.
  ZC_NODISCARD virtual zc::Maybe<zc::Vector<identity::GenericParameterKey>> genericParameterKeys(
      identity::DefId definition) const = 0;
  /// \brief Returns the decoded substitution record for an instance edge.
  ZC_NODISCARD virtual zc::Maybe<const checker::checked::SubstitutionData&> substitution(
      checker::checked::CanonicalSubstitutionId id) const = 0;
  /// \brief Returns the canonical substitution record bytes.
  ZC_NODISCARD virtual zc::Maybe<zc::ArrayPtr<const uint8_t>> substitutionRecord(
      checker::checked::CanonicalSubstitutionId id) const = 0;
  /// \brief Returns the canonical witness record bytes.
  ZC_NODISCARD virtual zc::Maybe<zc::ArrayPtr<const uint8_t>> witnessRecord(
      checker::checked::WitnessArgumentsId id) const = 0;
  /// \brief Returns the 32-byte canonical definition key bytes.
  ZC_NODISCARD virtual zc::Maybe<zc::Array<uint8_t>> definitionKey(
      identity::DefId definition) const = 0;
};

/// \brief One collected instance before canonical body encoding.
struct MonoRawInstance final {
  ir::InstanceId identity;
  mir::MirFunction body;
  zc::Array<uint8_t> identityFrame;
  identity::SourceSpan callSiteSpan;
  uint64_t substitutedNodeCount;
};

/// \brief Recursive instantiation: one definition repeats with a growing chain.
struct MonoCycleError final {
  ir::InstanceId root;
  zc::Vector<ir::InstanceId> chain;
  identity::SourceSpan span;
};

/// \brief Fixed instance or substitution-node budget exceeded.
struct MonoBudgetError final {
  ir::InstanceId root;
  zc::Vector<ir::InstanceId> chain;
  uint64_t requestedInstanceCount;
  uint64_t requestedSubstitutionNodeCount;
  uint64_t instanceLimit;
  uint64_t substitutionNodeLimit;
  identity::SourceSpan span;
};

/// \brief An instance edge named no resolvable template or record.
struct MonoUnresolvedEdgeError final {
  ir::InstanceId instance;
  identity::SourceSpan span;
};

using MonoFixpointResult =
    zc::OneOf<zc::Vector<MonoRawInstance>, MonoCycleError, MonoBudgetError, MonoUnresolvedEdgeError>;

/// \brief Runs the deterministic instance fixpoint over source roots.
///
/// Scans roots in function/block/terminator order, then each newly appended
/// instance FIFO. A previously seen instance is shared (same-substitution
/// recursion is idempotent). An unseen instance whose definition is already on
/// the expansion chain selects the cycle error; the fixed instance and node
/// budgets select the budget error. Output is in first-sight order; callers
/// byte-sort identity frames for the final artifact.
ZC_NODISCARD MonoFixpointResult collectInstances(
    zc::ArrayPtr<const mir::MirFunction* const> roots, const MonoEvidenceProvider& provider,
    type::SemanticTypeStore& semanticTypes, uint64_t instanceBudget,
    uint64_t substitutionNodeBudget);

/// \brief Sorts raw instances by their length-framed identity bytes.
void sortInstancesByIdentity(zc::Vector<MonoRawInstance>& instances) noexcept;

/// \brief Body predicate failure kinds of the independent instance verifier.
enum class MonoBodyRejectionKind : uint8_t {
  ResidualGenericKey = 0x01,
  NonScalarCarrier = 0x02,
  ReferenceCarrier = 0x03,
  BorrowCreationStatement = 0x04,
  UnsafeScopeBoundaryStatement = 0x05,
  DeinitializeStatement = 0x06,
  ReceiverActivationEffect = 0x07,
};

/// \brief One located RFC 0056 instance-body predicate failure.
struct MonoBodyRejection final {
  MonoBodyRejectionKind kind;
  uint32_t blockOrdinal;
  uint32_t statementOrdinal;
};

/// \brief Enforces residual-key absence, scalar carrier membership, and
/// ownership triviality on one substituted instance body.
///
/// Every type slot must be an admitted primitive (i32 or bool) that is not a
/// Reference or RawPointer and must not mention one of the callee's generic
/// parameter keys; the body must contain zero BorrowCreation, zero
/// UnsafeScopeBoundary, and zero Deinitialize statements; every call edge must
/// carry the no-activation effect.
ZC_NODISCARD zc::Maybe<MonoBodyRejection> validateInstanceBody(
    const mir::MirFunction& body,
    zc::ArrayPtr<const identity::GenericParameterKey> calleeKeys,
    const type::SemanticTypeStore& semanticTypes) noexcept;

/// \brief Edge predicate failure kinds.
enum class MonoEdgeRejectionKind : uint8_t {
  UnresolvedCallee = 0x01,
  ParameterKeyMismatch = 0x02,
  ArityMismatch = 0x03,
  EdgeNotClosed = 0x04,
};

/// \brief One located call-edge key, arity, or closure failure.
struct MonoEdgeRejection final {
  MonoEdgeRejectionKind kind;
  uint32_t blockOrdinal;
};

/// \brief Validates every Instance edge in one body against callee generic
/// parameters and the member-instance closure.
///
/// Each edge's substitution parameters must equal the callee's ordered generic
/// parameter keys in count and content, and every edge must resolve to a member
/// instance of the published set.
ZC_NODISCARD zc::Maybe<MonoEdgeRejection> validateCallEdges(
    const mir::MirFunction& body, const MonoEvidenceProvider& provider,
    zc::FunctionParam<bool(ir::InstanceId)> isMember) noexcept;

/// \brief Returns whether instances are ordered by ascending identity bytes.
ZC_NODISCARD bool instancesAreFrameSorted(
    zc::ArrayPtr<const MonoRawInstance> instances) noexcept;

}  // namespace zomlang::compiler::mono
