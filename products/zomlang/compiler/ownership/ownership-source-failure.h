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
#include "zomlang/compiler/diagnostics/core/diagnostic-ids.h"
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

/// \brief Canonical identity+span cause for a conflicting borrow.
///
/// `loan` is the conflicting loan, `source` is the loan's referenced move path,
/// `event` is the span-source-map validation event, and `span` is the source-map
/// span for `loan.issue`.
struct LoanFailureCause final {
  LoanKey loan;
  facts::MovePathKey source;
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
///
/// Produced by `BorrowSourceVerifier` for a mutable borrow whose place carries
/// an active overlapping loan. The liveness region is the event-granular NLL
/// span from the loan's activation to the last read of its destination.
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
///
/// Produced by `BorrowSourceVerifier` for a shared borrow whose place carries
/// an active mutable loan. Two shared loans never conflict.
struct SharedBorrowConflictFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<LoanFailureCause> causes;
};

/// \brief Borrowed value does not live long enough.
///
/// Produced by `BorrowSourceVerifier` for a returned reference whose origin is
/// a function-local binding. A returned reference may originate only from a
/// parameter or receiver; a local reference cannot escape because its storage
/// dies before the caller can use it.
struct BorrowDoesNotLiveLongEnoughFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<EscapeFailureCause> causes;
};

/// \brief Linear value is not consumed on all normal paths.
///
/// Produced by `OwnershipResourceVerifier::verifyLinearSource` for a linear
/// obligation whose consumptions sequence is empty on a normal exit. With the
/// current straight-line MIR subset, an obligation is pending at the normal
/// exit exactly when it has no Return or ConsumingCall consumption.
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
///
/// The RFC 0007 record carries no move path: the complete boundary key is the
/// canonical anchor, and `primary` must equal `boundary.event`.
struct RawPointerBoundaryRequiresUnsafeFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  uint32_t traversalOrdinal;
  UnsafeBoundaryKey boundary;
};

/// \brief Cannot move a value while it is borrowed.
///
/// Produced by `BorrowSourceVerifier` for a Move operand whose place carries
/// an active overlapping loan. Suppression rule 4 suppresses the UseAfterMove
/// cascade at a MoveOutOfBorrow primary because a blocked move does not move,
/// drop, or consume the place.
struct MoveOutOfBorrowFailure final {
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  facts::MovePathKey place;
  uint32_t traversalOrdinal;
  zc::Vector<LoanFailureCause> causes;
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

inline bool lessProjection(const mir::MirProjection& left,
                           const mir::MirProjection& right) noexcept {
  if (left.kind() != right.kind()) {
    return static_cast<uint8_t>(left.kind()) < static_cast<uint8_t>(right.kind());
  }
  switch (left.kind()) {
    case mir::MirProjectionKind::Field:
      // The field DefId is ordering-equivalent (equality only).
      return false;
    case mir::MirProjectionKind::Index:
      return left.indexValue().index.ordinal() < right.indexValue().index.ordinal();
    case mir::MirProjectionKind::Dereference:
      return false;
    case mir::MirProjectionKind::Downcast:
      // The variant DefId is ordering-equivalent (equality only).
      return false;
    case mir::MirProjectionKind::Subslice:
      if (left.subsliceValue().first != right.subsliceValue().first) {
        return left.subsliceValue().first < right.subsliceValue().first;
      }
      return left.subsliceValue().pastLast < right.subsliceValue().pastLast;
  }
  return false;
}

inline bool lessPlace(const mir::MirPlace& left, const mir::MirPlace& right) noexcept {
  if (left.local() != right.local()) return left.local().ordinal() < right.local().ordinal();
  if (left.projections().size() != right.projections().size()) {
    return left.projections().size() < right.projections().size();
  }
  for (size_t index = 0; index < left.projections().size(); ++index) {
    if (lessProjection(left.projections()[index], right.projections()[index])) return true;
    if (lessProjection(right.projections()[index], left.projections()[index])) return false;
  }
  return false;
}

inline bool lessMovePath(const facts::MovePathKey& left, const facts::MovePathKey& right) noexcept {
  // The owner DefId is ordering-equivalent (equality only); see the owner
  // rationale in OwnershipSourceFailureOrdering::less.
  if (left.owner != right.owner) return false;
  return lessPlace(left.place, right.place);
}

inline bool lessCause(const LoanFailureCause& left, const LoanFailureCause& right) noexcept {
  if (lessEvent(left.loan.issue, right.loan.issue)) return true;
  if (lessEvent(right.loan.issue, left.loan.issue)) return false;
  if (lessMovePath(left.source, right.source)) return true;
  if (lessMovePath(right.source, left.source)) return false;
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

inline bool lessBoundary(const UnsafeBoundaryKey& left, const UnsafeBoundaryKey& right) noexcept {
  if (lessEvent(left.event, right.event)) return true;
  if (lessEvent(right.event, left.event)) return false;
  return left.unsafeOrdinal < right.unsafeOrdinal;
}

inline bool equalSpan(const identity::SourceSpan& left,
                      const identity::SourceSpan& right) noexcept {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

inline bool equalProjection(const mir::MirProjection& left,
                            const mir::MirProjection& right) noexcept {
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

inline bool equalPlace(const mir::MirPlace& left, const mir::MirPlace& right) noexcept {
  if (left.local() != right.local() || left.rootType() != right.rootType() ||
      left.resultType() != right.resultType() ||
      left.projections().size() != right.projections().size()) {
    return false;
  }
  for (size_t index = 0; index < left.projections().size(); ++index) {
    if (!equalProjection(left.projections()[index], right.projections()[index])) return false;
  }
  return true;
}

inline bool equalMovePath(const facts::MovePathKey& left,
                          const facts::MovePathKey& right) noexcept {
  return left.owner == right.owner && equalPlace(left.place, right.place);
}

inline bool equalCause(const InitializationFailureCause& left,
                       const InitializationFailureCause& right) noexcept {
  return left.kind == right.kind && left.event == right.event && equalSpan(left.span, right.span);
}

inline bool equalCause(const MoveFailureCause& left, const MoveFailureCause& right) noexcept {
  return left.event == right.event && equalSpan(left.span, right.span);
}

inline bool equalCause(const LoanFailureCause& left, const LoanFailureCause& right) noexcept {
  return left.loan == right.loan && equalMovePath(left.source, right.source) &&
         left.event == right.event && equalSpan(left.span, right.span);
}

inline bool equalCause(const LinearConsumptionCause& left,
                       const LinearConsumptionCause& right) noexcept {
  return left.event == right.event && equalSpan(left.span, right.span);
}

inline bool equalCause(const EscapeFailureCause& left, const EscapeFailureCause& right) noexcept {
  return left.event == right.event && equalSpan(left.span, right.span);
}

template <typename Cause>
bool equalCauseSequence(const zc::Vector<Cause>& left, const zc::Vector<Cause>& right) noexcept {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (!equalCause(left[index], right[index])) return false;
  }
  return true;
}

template <typename Variant>
bool equalCauseFailure(const Variant& left, const OwnershipSourceFailure& right) noexcept {
  const auto& rightValue = right.get<Variant>();
  return left.owner == rightValue.owner && left.primary == rightValue.primary &&
         equalSpan(left.useSpan, rightValue.useSpan) &&
         equalMovePath(left.place, rightValue.place) &&
         left.traversalOrdinal == rightValue.traversalOrdinal &&
         equalCauseSequence(left.causes, rightValue.causes);
}

inline bool equalFailure(const OwnershipSourceFailure& left,
                         const OwnershipSourceFailure& right) noexcept {
  if (left.which() != right.which()) return false;
  ZC_SWITCH_ONEOF(left) {
    ZC_CASE_ONEOF(leftValue, UseAfterMoveFailure) { return equalCauseFailure(leftValue, right); }
    ZC_CASE_ONEOF(leftValue, MutableBorrowConflictFailure) {
      return equalCauseFailure(leftValue, right);
    }
    ZC_CASE_ONEOF(leftValue, UninitializedPlaceUseFailure) {
      return equalCauseFailure(leftValue, right);
    }
    ZC_CASE_ONEOF(leftValue, SharedBorrowConflictFailure) {
      return equalCauseFailure(leftValue, right);
    }
    ZC_CASE_ONEOF(leftValue, BorrowDoesNotLiveLongEnoughFailure) {
      return equalCauseFailure(leftValue, right);
    }
    ZC_CASE_ONEOF(leftValue, LinearNotConsumedFailure) {
      return equalCauseFailure(leftValue, right);
    }
    ZC_CASE_ONEOF(leftValue, LinearConsumedTwiceFailure) {
      return equalCauseFailure(leftValue, right);
    }
    ZC_CASE_ONEOF(leftValue, RawPointerBoundaryRequiresUnsafeFailure) {
      const auto& rightValue = right.get<RawPointerBoundaryRequiresUnsafeFailure>();
      return leftValue.owner == rightValue.owner && leftValue.primary == rightValue.primary &&
             equalSpan(leftValue.useSpan, rightValue.useSpan) &&
             leftValue.traversalOrdinal == rightValue.traversalOrdinal &&
             leftValue.boundary == rightValue.boundary;
    }
    ZC_CASE_ONEOF(leftValue, MoveOutOfBorrowFailure) { return equalCauseFailure(leftValue, right); }
  }
  return false;
}

inline const MirEventKey& primaryEvent(const OwnershipSourceFailure& failure) noexcept {
  ZC_SWITCH_ONEOF(failure) {
    ZC_CASE_ONEOF(value, UseAfterMoveFailure) { return value.primary; }
    ZC_CASE_ONEOF(value, MutableBorrowConflictFailure) { return value.primary; }
    ZC_CASE_ONEOF(value, UninitializedPlaceUseFailure) { return value.primary; }
    ZC_CASE_ONEOF(value, SharedBorrowConflictFailure) { return value.primary; }
    ZC_CASE_ONEOF(value, BorrowDoesNotLiveLongEnoughFailure) { return value.primary; }
    ZC_CASE_ONEOF(value, LinearNotConsumedFailure) { return value.primary; }
    ZC_CASE_ONEOF(value, LinearConsumedTwiceFailure) { return value.primary; }
    ZC_CASE_ONEOF(value, RawPointerBoundaryRequiresUnsafeFailure) { return value.primary; }
    ZC_CASE_ONEOF(value, MoveOutOfBorrowFailure) { return value.primary; }
  }
  ZC_UNREACHABLE
}

inline uint32_t primaryDiagId(const OwnershipSourceFailure& failure) noexcept {
  using diagnostics::DiagID;
  static constexpr uint32_t ids[9] = {
      static_cast<uint32_t>(DiagID::UseAfterMove),
      static_cast<uint32_t>(DiagID::MutableBorrowConflicts),
      static_cast<uint32_t>(DiagID::UninitializedPlaceUse),
      static_cast<uint32_t>(DiagID::SharedBorrowConflicts),
      static_cast<uint32_t>(DiagID::BorrowDoesNotLiveLongEnough),
      static_cast<uint32_t>(DiagID::LinearNotConsumed),
      static_cast<uint32_t>(DiagID::LinearConsumedTwice),
      static_cast<uint32_t>(DiagID::RawPointerBoundaryRequiresUnsafe),
      static_cast<uint32_t>(DiagID::MoveOutOfBorrow),
  };
  return ids[static_cast<uint8_t>(failure.which())];
}

struct FailureCommonKey {
  uint64_t spanStart;
  uint64_t spanEnd;
  uint32_t diagId;
  uint32_t ordinal;
  identity::DefId owner;
  uint8_t tag;
};

#define ZOM_OWNERSHIP_FAILURE_COMMON_KEY(variantType) \
  ZC_CASE_ONEOF(value, variantType) {                 \
    key.spanStart = value.useSpan.byteStart();        \
    key.spanEnd = value.useSpan.byteEnd();            \
    key.ordinal = value.traversalOrdinal;             \
    key.owner = value.owner;                          \
  }

inline FailureCommonKey commonKey(const OwnershipSourceFailure& failure) noexcept {
  FailureCommonKey key{
      0, 0, primaryDiagId(failure), 0, identity::DefId{}, static_cast<uint8_t>(failure.which())};
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

#define ZOM_OWNERSHIP_FAILURE_CAUSES(variantType)                  \
  ZC_CASE_ONEOF(leftValue, variantType) {                          \
    const auto& rightValue = right.get<variantType>();             \
    return lessCauseSequence(leftValue.causes, rightValue.causes); \
  }

inline bool lessPayload(const OwnershipSourceFailure& left,
                        const OwnershipSourceFailure& right) noexcept {
  ZC_SWITCH_ONEOF(left) {
    ZOM_OWNERSHIP_FAILURE_CAUSES(UseAfterMoveFailure)
    ZOM_OWNERSHIP_FAILURE_CAUSES(MutableBorrowConflictFailure)
    ZOM_OWNERSHIP_FAILURE_CAUSES(UninitializedPlaceUseFailure)
    ZOM_OWNERSHIP_FAILURE_CAUSES(SharedBorrowConflictFailure)
    ZOM_OWNERSHIP_FAILURE_CAUSES(BorrowDoesNotLiveLongEnoughFailure)
    ZOM_OWNERSHIP_FAILURE_CAUSES(LinearNotConsumedFailure)
    ZOM_OWNERSHIP_FAILURE_CAUSES(LinearConsumedTwiceFailure)
    ZC_CASE_ONEOF(leftValue, RawPointerBoundaryRequiresUnsafeFailure) {
      const auto& rightValue = right.get<RawPointerBoundaryRequiresUnsafeFailure>();
      return lessBoundary(leftValue.boundary, rightValue.boundary);
    }
    ZOM_OWNERSHIP_FAILURE_CAUSES(MoveOutOfBorrowFailure)
  }
  return false;
}

#undef ZOM_OWNERSHIP_FAILURE_CAUSES

}  // namespace detail

/// \brief Canonical global ordering for closed ownership source failures.
///
/// Order: primary span byteStart, byteEnd, numeric primary diagnostic ID,
/// traversalOrdinal, expanded owner key, primary MirEventKey, variant tag, then
/// the remaining complete payload (cause sequence or unsafe boundary).
///
/// The owner DefId exposes equality but no public ordering; the RFC's expanded
/// owner key requires identity-authority expansion that this header cannot
/// perform. Distinct owners are ordering-equivalent here. traversalOrdinal is
/// unique per failure within one analysis batch, so owner equivalence never
/// decides the within-batch order; `equalFailure` still distinguishes distinct
/// owners for deduplication.
struct OwnershipSourceFailureOrdering final {
  ZC_NODISCARD static bool less(const OwnershipSourceFailure& left,
                                const OwnershipSourceFailure& right) noexcept {
    const auto leftKey = detail::commonKey(left);
    const auto rightKey = detail::commonKey(right);
    if (leftKey.spanStart != rightKey.spanStart) return leftKey.spanStart < rightKey.spanStart;
    if (leftKey.spanEnd != rightKey.spanEnd) return leftKey.spanEnd < rightKey.spanEnd;
    if (leftKey.diagId != rightKey.diagId) return leftKey.diagId < rightKey.diagId;
    if (leftKey.ordinal != rightKey.ordinal) return leftKey.ordinal < rightKey.ordinal;
    // Owner is ordering-equivalent; see the struct comment.
    if (detail::lessEvent(detail::primaryEvent(left), detail::primaryEvent(right))) return true;
    if (detail::lessEvent(detail::primaryEvent(right), detail::primaryEvent(left))) return false;
    if (leftKey.tag != rightKey.tag) return leftKey.tag < rightKey.tag;
    return detail::lessPayload(left, right);
  }

  /// \brief Returns whether two failures are byte-identical for deduplication.
  ZC_NODISCARD static bool equal(const OwnershipSourceFailure& left,
                                 const OwnershipSourceFailure& right) noexcept {
    return detail::equalFailure(left, right);
  }

  /// \brief Sorts failures by the canonical ordering and removes adjacent
  /// byte-identical failures, returning the deduplicated sequence.
  ZC_NODISCARD static zc::Vector<OwnershipSourceFailure> deduplicate(
      zc::Vector<OwnershipSourceFailure>&& failures) {
    for (size_t index = 1; index < failures.size(); ++index) {
      auto current = zc::mv(failures[index]);
      size_t insertion = index;
      while (insertion != 0 && less(current, failures[insertion - 1])) {
        failures[insertion] = zc::mv(failures[insertion - 1]);
        --insertion;
      }
      failures[insertion] = zc::mv(current);
    }
    size_t write = 0;
    for (size_t read = 0; read < failures.size(); ++read) {
      if (write != 0 && detail::equalFailure(failures[read], failures[write - 1])) continue;
      if (write != read) failures[write] = zc::mv(failures[read]);
      ++write;
    }
    failures.truncate(write);
    return zc::mv(failures);
  }
};

}  // namespace zomlang::compiler::ownership
