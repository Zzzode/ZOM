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

#pragma once

#include <cstdint>

#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/handle.h"
#include "zomlang/compiler/identity/key/source-key.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership {
namespace facts {

/// \brief The current-subset reason a root local is unavailable at one MIR point.
enum class InitializationLossKind : uint8_t {
  NeverInitialized = 0x01,
  Moved = 0x02,
  Deinitialized = 0x03,
  StorageEnded = 0x04,
};

}  // namespace facts

/// \brief One unavailable-state cause retained for a source-visible root-local use.
struct InitializationFailureCause final {
  facts::InitializationLossKind kind;
  MirEventKey event;
  identity::SourceSpan span;
};

/// \brief Minimal canonical identity+span cause for a move out of a borrowed place.
/// Phase B populates the full move provenance.
struct MoveFailureCause final {
  MirEventKey event;
  identity::SourceSpan span;
};

/// \brief Minimal canonical identity+span cause for a conflicting borrow.
/// Phase B populates the full loan provenance.
struct LoanFailureCause final {
  MirEventKey event;
  identity::SourceSpan span;
};

/// \brief Minimal canonical identity+span cause for a linear value consumption.
/// Phase B populates the full consumption provenance.
struct LinearConsumptionCause final {
  MirEventKey event;
  identity::SourceSpan span;
};

/// \brief Minimal canonical identity+span cause for a borrow escape.
/// Phase B populates the full escape provenance.
struct EscapeFailureCause final {
  MirEventKey event;
  identity::SourceSpan span;
};

/// \brief Use of a moved value.
struct UseAfterMoveFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<InitializationFailureCause> causes;
};

/// \brief Mutable borrow conflicts with an active borrow.
struct MutableBorrowConflictFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<LoanFailureCause> causes;
};

/// \brief Use of a place that is not initialized.
struct UninitializedPlaceUseFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<InitializationFailureCause> causes;
};

/// \brief Shared borrow conflicts with an active mutable borrow.
struct SharedBorrowConflictFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<LoanFailureCause> causes;
};

/// \brief Borrowed value does not live long enough.
struct BorrowDoesNotLiveLongEnoughFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<EscapeFailureCause> causes;
};

/// \brief Linear value is not consumed on all normal paths.
struct LinearNotConsumedFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<LinearConsumptionCause> causes;
};

/// \brief Linear value is consumed more than once.
struct LinearConsumedTwiceFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<LinearConsumptionCause> causes;
};

/// \brief Raw pointer boundary requires an unsafe block.
struct RawPointerBoundaryRequiresUnsafeFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
};

/// \brief Cannot move a value while it is borrowed.
struct MoveOutOfBorrowFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<MoveFailureCause> causes;
};

/// \brief Closed tagged union of every source-visible ownership failure.
///
/// Variant declaration order fixes the canonical tag order used by
/// `OwnershipSourceFailureOrdering`.
using OwnershipSourceFailure =
    zc::OneOf<UseAfterMoveFailure, MutableBorrowConflictFailure, UninitializedPlaceUseFailure,
              SharedBorrowConflictFailure, BorrowDoesNotLiveLongEnoughFailure,
              LinearNotConsumedFailure, LinearConsumedTwiceFailure,
              RawPointerBoundaryRequiresUnsafeFailure, MoveOutOfBorrowFailure>;

namespace detail {

inline bool lessEvent(const MirEventKey& left, const MirEventKey& right) noexcept {
  if (left.location.point < right.location.point) return true;
  if (right.location.point < left.location.point) return false;
  return left.operandOrdinal < right.operandOrdinal;
}

inline bool lessSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) noexcept {
  if (left.byteStart() != right.byteStart()) return left.byteStart() < right.byteStart();
  return left.byteEnd() < right.byteEnd();
}

inline bool lessCause(const InitializationFailureCause& left,
                      const InitializationFailureCause& right) noexcept {
  if (left.kind != right.kind) {
    return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
  }
  if (lessEvent(left.event, right.event)) return true;
  if (lessEvent(right.event, left.event)) return false;
  return lessSpan(left.span, right.span);
}

inline bool lessCause(const MoveFailureCause& left, const MoveFailureCause& right) noexcept {
  if (lessEvent(left.event, right.event)) return true;
  if (lessEvent(right.event, left.event)) return false;
  return lessSpan(left.span, right.span);
}

inline bool lessCause(const LoanFailureCause& left, const LoanFailureCause& right) noexcept {
  if (lessEvent(left.event, right.event)) return true;
  if (lessEvent(right.event, left.event)) return false;
  return lessSpan(left.span, right.span);
}

inline bool lessCause(const LinearConsumptionCause& left,
                      const LinearConsumptionCause& right) noexcept {
  if (lessEvent(left.event, right.event)) return true;
  if (lessEvent(right.event, left.event)) return false;
  return lessSpan(left.span, right.span);
}

inline bool lessCause(const EscapeFailureCause& left, const EscapeFailureCause& right) noexcept {
  if (lessEvent(left.event, right.event)) return true;
  if (lessEvent(right.event, left.event)) return false;
  return lessSpan(left.span, right.span);
}

template <typename Cause>
bool lessCauseSequence(const zc::Vector<Cause>& left, const zc::Vector<Cause>& right) noexcept {
  if (left.size() != right.size()) return left.size() < right.size();
  for (size_t index = 0; index < left.size(); ++index) {
    if (lessCause(left[index], right[index])) return true;
    if (lessCause(right[index], left[index])) return false;
  }
  return false;
}

struct FailureCommonKey {
  uint64_t spanStart;
  uint64_t spanEnd;
  uint32_t ordinal;
  uint8_t tag;
};

#define ZOM_OWNERSHIP_FAILURE_COMMON_KEY(variantType) \
  ZC_CASE_ONEOF(value, variantType) {                 \
    key.spanStart = value.useSpan.byteStart();        \
    key.spanEnd = value.useSpan.byteEnd();            \
    key.ordinal = value.traversalOrdinal;             \
  }

inline FailureCommonKey commonKey(const OwnershipSourceFailure& failure) noexcept {
  FailureCommonKey key{0, 0, 0, static_cast<uint8_t>(failure.which())};
  ZC_SWITCH_ONEOF(failure) {
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(UseAfterMoveFailure)
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(MutableBorrowConflictFailure)
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(UninitializedPlaceUseFailure)
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(SharedBorrowConflictFailure)
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(BorrowDoesNotLiveLongEnoughFailure)
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(LinearNotConsumedFailure)
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(LinearConsumedTwiceFailure)
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(RawPointerBoundaryRequiresUnsafeFailure)
    ZOM_OWNERSHIP_FAILURE_COMMON_KEY(MoveOutOfBorrowFailure)
  }
  return key;
}

#undef ZOM_OWNERSHIP_FAILURE_COMMON_KEY

#define ZOM_OWNERSHIP_FAILURE_PAYLOAD(variantType)                      \
  ZC_CASE_ONEOF(leftValue, variantType) {                               \
    const auto& rightValue = right.get<variantType>();                  \
    if (lessEvent(leftValue.primary, rightValue.primary)) return true;  \
    if (lessEvent(rightValue.primary, leftValue.primary)) return false; \
    return lessCauseSequence(leftValue.causes, rightValue.causes);      \
  }

inline bool lessPayload(const OwnershipSourceFailure& left,
                        const OwnershipSourceFailure& right) noexcept {
  ZC_SWITCH_ONEOF(left) {
    ZOM_OWNERSHIP_FAILURE_PAYLOAD(UseAfterMoveFailure)
    ZOM_OWNERSHIP_FAILURE_PAYLOAD(MutableBorrowConflictFailure)
    ZOM_OWNERSHIP_FAILURE_PAYLOAD(UninitializedPlaceUseFailure)
    ZOM_OWNERSHIP_FAILURE_PAYLOAD(SharedBorrowConflictFailure)
    ZOM_OWNERSHIP_FAILURE_PAYLOAD(BorrowDoesNotLiveLongEnoughFailure)
    ZOM_OWNERSHIP_FAILURE_PAYLOAD(LinearNotConsumedFailure)
    ZOM_OWNERSHIP_FAILURE_PAYLOAD(LinearConsumedTwiceFailure)
    ZC_CASE_ONEOF(leftValue, RawPointerBoundaryRequiresUnsafeFailure) {
      const auto& rightValue = right.get<RawPointerBoundaryRequiresUnsafeFailure>();
      if (lessEvent(leftValue.primary, rightValue.primary)) return true;
      if (lessEvent(rightValue.primary, leftValue.primary)) return false;
      return false;
    }
    ZOM_OWNERSHIP_FAILURE_PAYLOAD(MoveOutOfBorrowFailure)
  }
  return false;
}

#undef ZOM_OWNERSHIP_FAILURE_PAYLOAD

}  // namespace detail

/// \brief Canonical global ordering for closed ownership source failures.
///
/// Order: primary span byteStart, byteEnd, traversalOrdinal, variant tag, then
/// the remaining payload (primary event, cause sequence).
struct OwnershipSourceFailureOrdering final {
  ZC_NODISCARD static bool less(const OwnershipSourceFailure& left,
                                const OwnershipSourceFailure& right) noexcept {
    const auto leftKey = detail::commonKey(left);
    const auto rightKey = detail::commonKey(right);
    if (leftKey.spanStart != rightKey.spanStart) return leftKey.spanStart < rightKey.spanStart;
    if (leftKey.spanEnd != rightKey.spanEnd) return leftKey.spanEnd < rightKey.spanEnd;
    if (leftKey.ordinal != rightKey.ordinal) return leftKey.ordinal < rightKey.ordinal;
    if (leftKey.tag != rightKey.tag) return leftKey.tag < rightKey.tag;
    return detail::lessPayload(left, right);
  }
};

}  // namespace zomlang::compiler::ownership
