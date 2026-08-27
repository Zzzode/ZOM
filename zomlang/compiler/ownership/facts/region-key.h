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

#include "zc/core/common.h"
#include "zomlang/compiler/identity/handle.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership::facts {

// ---------------------------------------------------------------------------
// Comparison helpers (mirrors the patterns in ownership-source-failure.h)
// ---------------------------------------------------------------------------

/// \brief Orders two MirEventKey values by point then operand ordinal.
inline bool lessEventKey(const MirEventKey& left, const MirEventKey& right) noexcept {
  if (left.location.point < right.location.point) return true;
  if (right.location.point < left.location.point) return false;
  return left.operandOrdinal < right.operandOrdinal;
}

/// \brief Orders two MirProjection values.
inline bool lessProjectionKey(const mir::MirProjection& left,
                              const mir::MirProjection& right) noexcept {
  if (left.kind() != right.kind()) {
    return static_cast<uint8_t>(left.kind()) < static_cast<uint8_t>(right.kind());
  }
  switch (left.kind()) {
    case mir::MirProjectionKind::Field:
      return left.fieldValue().field < right.fieldValue().field;
    case mir::MirProjectionKind::Index:
      return left.indexValue().index.ordinal() < right.indexValue().index.ordinal();
    case mir::MirProjectionKind::Dereference:
      return false;
    case mir::MirProjectionKind::Downcast:
      return left.downcastValue().variant < right.downcastValue().variant;
    case mir::MirProjectionKind::Subslice:
      if (left.subsliceValue().first != right.subsliceValue().first) {
        return left.subsliceValue().first < right.subsliceValue().first;
      }
      return left.subsliceValue().pastLast < right.subsliceValue().pastLast;
  }
  return false;
}

/// \brief Orders two MirPlace values by local then projections.
inline bool lessPlaceKey(const mir::MirPlace& left, const mir::MirPlace& right) noexcept {
  if (left.local() != right.local()) return left.local().ordinal() < right.local().ordinal();
  if (left.projections().size() != right.projections().size()) {
    return left.projections().size() < right.projections().size();
  }
  for (size_t index = 0; index < left.projections().size(); ++index) {
    if (lessProjectionKey(left.projections()[index], right.projections()[index])) return true;
    if (lessProjectionKey(right.projections()[index], left.projections()[index])) return false;
  }
  return false;
}

/// \brief Orders two MovePathKey values by owner then place.
inline bool lessMovePathKey(const MovePathKey& left, const MovePathKey& right) noexcept {
  if (left.owner != right.owner) return left.owner < right.owner;
  return lessPlaceKey(left.place, right.place);
}

// ---------------------------------------------------------------------------
// BorrowInputKey
// ---------------------------------------------------------------------------

/// \brief Receiver borrow input root.
struct BorrowInputReceiver final {
  bool operator==(const BorrowInputReceiver&) const = default;
};

/// \brief Parameter borrow input root.
struct BorrowInputParameter final {
  uint32_t index = 0;

  bool operator==(const BorrowInputParameter&) const = default;
};

/// \brief Canonical identity of one borrow input (receiver or parameter).
///
/// Tags are `Receiver = 0x01` and `Parameter = 0x02`, matching RFC 0013.
class BorrowInputKey final {
public:
  ZC_NODISCARD static BorrowInputKey receiver() noexcept {
    return BorrowInputKey(BorrowInputReceiver{});
  }
  ZC_NODISCARD static BorrowInputKey parameter(uint32_t index) noexcept {
    return BorrowInputKey(BorrowInputParameter{index});
  }

  ZC_NODISCARD bool isReceiver() const noexcept { return value.is<BorrowInputReceiver>(); }
  ZC_NODISCARD bool isParameter() const noexcept { return value.is<BorrowInputParameter>(); }
  ZC_NODISCARD const BorrowInputReceiver& receiverValue() const {
    return value.get<BorrowInputReceiver>();
  }
  ZC_NODISCARD const BorrowInputParameter& parameterValue() const {
    return value.get<BorrowInputParameter>();
  }

  bool operator==(const BorrowInputKey& other) const noexcept {
    if (isReceiver() != other.isReceiver()) return false;
    if (isReceiver()) return true;
    return parameterValue().index == other.parameterValue().index;
  }
  bool operator!=(const BorrowInputKey& other) const noexcept { return !(*this == other); }
  bool operator<(const BorrowInputKey& other) const noexcept {
    const uint8_t leftTag = isReceiver() ? 0x01 : 0x02;
    const uint8_t rightTag = other.isReceiver() ? 0x01 : 0x02;
    if (leftTag != rightTag) return leftTag < rightTag;
    if (isReceiver()) return false;
    return parameterValue().index < other.parameterValue().index;
  }

private:
  explicit BorrowInputKey(zc::OneOf<BorrowInputReceiver, BorrowInputParameter> v) noexcept
      : value(zc::mv(v)) {}

  zc::OneOf<BorrowInputReceiver, BorrowInputParameter> value;
};

// ---------------------------------------------------------------------------
// RegionKey
// ---------------------------------------------------------------------------

/// \brief Static region keyed by its owning definition.
struct StaticRegion final {
  identity::DefId owner;
};

/// \brief Borrow-input region keyed by its owning definition and input root.
struct InputRegion final {
  identity::DefId owner;
  BorrowInputKey input;
};

/// \brief Loan region keyed by its loan identity.
struct LoanRegion final {
  LoanKey loan;
};

/// \brief Physical storage region keyed by its live event and root move path.
struct StorageRegion final {
  MirEventKey live;
  MovePathKey root;
};

/// \brief NLL reference-value region keyed by its introduction event and destination.
struct LocalValueRegion final {
  MirEventKey introduction;
  MovePathKey destination;
};

/// \brief Closure-value region keyed by its construction event and closure move path.
struct ClosureValueRegion final {
  MirEventKey construction;
  MovePathKey closure;
};

/// \brief Canonical identity of one region in the RFC 0007 region algebra.
///
/// Tags are `0x01` through `0x06` in declaration order (Static, Input, Loan,
/// Storage, LocalValue, ClosureValue). Region keys order by complete canonical
/// bytes: variant tag first, then field-wise comparison.
class RegionKey final {
public:
  ZC_NODISCARD static RegionKey staticRegion(identity::DefId owner) {
    return RegionKey(StaticRegion{owner});
  }
  ZC_NODISCARD static RegionKey inputRegion(identity::DefId owner, BorrowInputKey input) {
    return RegionKey(InputRegion{owner, zc::mv(input)});
  }
  ZC_NODISCARD static RegionKey loanRegion(LoanKey loan) { return RegionKey(LoanRegion{loan}); }
  ZC_NODISCARD static RegionKey storageRegion(MirEventKey live, MovePathKey root) {
    return RegionKey(StorageRegion{zc::mv(live), zc::mv(root)});
  }
  ZC_NODISCARD static RegionKey localValueRegion(MirEventKey introduction,
                                                 MovePathKey destination) {
    return RegionKey(LocalValueRegion{zc::mv(introduction), zc::mv(destination)});
  }
  ZC_NODISCARD static RegionKey closureValueRegion(MirEventKey construction, MovePathKey closure) {
    return RegionKey(ClosureValueRegion{zc::mv(construction), zc::mv(closure)});
  }

  ZC_NODISCARD bool isStatic() const noexcept { return value.is<StaticRegion>(); }
  ZC_NODISCARD bool isInput() const noexcept { return value.is<InputRegion>(); }
  ZC_NODISCARD bool isLoan() const noexcept { return value.is<LoanRegion>(); }
  ZC_NODISCARD bool isStorage() const noexcept { return value.is<StorageRegion>(); }
  ZC_NODISCARD bool isLocalValue() const noexcept { return value.is<LocalValueRegion>(); }
  ZC_NODISCARD bool isClosureValue() const noexcept { return value.is<ClosureValueRegion>(); }

  ZC_NODISCARD const StaticRegion& staticValue() const { return value.get<StaticRegion>(); }
  ZC_NODISCARD const InputRegion& inputValue() const { return value.get<InputRegion>(); }
  ZC_NODISCARD const LoanRegion& loanValue() const { return value.get<LoanRegion>(); }
  ZC_NODISCARD const StorageRegion& storageValue() const { return value.get<StorageRegion>(); }
  ZC_NODISCARD const LocalValueRegion& localValueValue() const {
    return value.get<LocalValueRegion>();
  }
  ZC_NODISCARD const ClosureValueRegion& closureValueValue() const {
    return value.get<ClosureValueRegion>();
  }

  /// \brief Returns the canonical variant tag (0x01-0x06).
  ZC_NODISCARD uint8_t tag() const noexcept {
    if (isStatic()) return 0x01;
    if (isInput()) return 0x02;
    if (isLoan()) return 0x03;
    if (isStorage()) return 0x04;
    if (isLocalValue()) return 0x05;
    return 0x06;
  }

  /// \brief Returns a deep copy of this region key.
  ZC_NODISCARD RegionKey clone() const {
    if (isStatic()) return RegionKey(StaticRegion{staticValue().owner});
    if (isInput()) { return RegionKey(InputRegion{inputValue().owner, inputValue().input}); }
    if (isLoan()) return RegionKey(LoanRegion{loanValue().loan});
    if (isStorage()) {
      const auto& storage = storageValue();
      return RegionKey(
          StorageRegion{storage.live, MovePathKey{storage.root.owner, storage.root.place.clone()}});
    }
    if (isLocalValue()) {
      const auto& local = localValueValue();
      return RegionKey(
          LocalValueRegion{local.introduction,
                           MovePathKey{local.destination.owner, local.destination.place.clone()}});
    }
    const auto& closure = closureValueValue();
    return RegionKey(ClosureValueRegion{
        closure.construction, MovePathKey{closure.closure.owner, closure.closure.place.clone()}});
  }

  bool operator==(const RegionKey& other) const noexcept {
    if (tag() != other.tag()) return false;
    if (isStatic()) return staticValue().owner == other.staticValue().owner;
    if (isInput()) {
      const auto& left = inputValue();
      const auto& right = other.inputValue();
      return left.owner == right.owner && left.input == right.input;
    }
    if (isLoan()) return loanValue().loan.issue == other.loanValue().loan.issue;
    if (isStorage()) {
      const auto& left = storageValue();
      const auto& right = other.storageValue();
      return left.live == right.live && !lessMovePathKey(left.root, right.root) &&
             !lessMovePathKey(right.root, left.root);
    }
    if (isLocalValue()) {
      const auto& left = localValueValue();
      const auto& right = other.localValueValue();
      return left.introduction == right.introduction &&
             !lessMovePathKey(left.destination, right.destination) &&
             !lessMovePathKey(right.destination, left.destination);
    }
    const auto& left = closureValueValue();
    const auto& right = other.closureValueValue();
    return left.construction == right.construction &&
           !lessMovePathKey(left.closure, right.closure) &&
           !lessMovePathKey(right.closure, left.closure);
  }
  bool operator!=(const RegionKey& other) const noexcept { return !(*this == other); }

  /// \brief Orders by canonical bytes: variant tag, then field-wise comparison.
  bool operator<(const RegionKey& other) const noexcept {
    if (tag() != other.tag()) return tag() < other.tag();
    if (isStatic()) return staticValue().owner < other.staticValue().owner;
    if (isInput()) {
      const auto& left = inputValue();
      const auto& right = other.inputValue();
      if (left.owner != right.owner) return left.owner < right.owner;
      return left.input < right.input;
    }
    if (isLoan()) return lessEventKey(loanValue().loan.issue, other.loanValue().loan.issue);
    if (isStorage()) {
      const auto& left = storageValue();
      const auto& right = other.storageValue();
      if (lessEventKey(left.live, right.live)) return true;
      if (lessEventKey(right.live, left.live)) return false;
      return lessMovePathKey(left.root, right.root);
    }
    if (isLocalValue()) {
      const auto& left = localValueValue();
      const auto& right = other.localValueValue();
      if (lessEventKey(left.introduction, right.introduction)) return true;
      if (lessEventKey(right.introduction, left.introduction)) return false;
      return lessMovePathKey(left.destination, right.destination);
    }
    const auto& left = closureValueValue();
    const auto& right = other.closureValueValue();
    if (lessEventKey(left.construction, right.construction)) return true;
    if (lessEventKey(right.construction, left.construction)) return false;
    return lessMovePathKey(left.closure, right.closure);
  }

private:
  explicit RegionKey(zc::OneOf<StaticRegion, InputRegion, LoanRegion, StorageRegion,
                               LocalValueRegion, ClosureValueRegion>
                         v) noexcept
      : value(zc::mv(v)) {}

  zc::OneOf<StaticRegion, InputRegion, LoanRegion, StorageRegion, LocalValueRegion,
            ClosureValueRegion>
      value;
};

// ---------------------------------------------------------------------------
// ReferenceRoot and ReferenceOrigin
// ---------------------------------------------------------------------------

/// \brief The root introduction of one reference origin.
///
/// `ReferenceRoot.region` must be `Static`, `Input`, or `Loan`; storage, value,
/// and closure regions never masquerade as reference authority.
struct ReferenceRoot final {
  RegionKey region;
  MovePathKey referent;
  MirEventKey introduction;

  ZC_NODISCARD ReferenceRoot clone() const {
    return ReferenceRoot{region.clone(), MovePathKey{referent.owner, referent.place.clone()},
                         introduction};
  }

  bool operator==(const ReferenceRoot& other) const noexcept {
    return region == other.region && !lessMovePathKey(referent, other.referent) &&
           !lessMovePathKey(other.referent, referent) && introduction == other.introduction;
  }
  bool operator!=(const ReferenceRoot& other) const noexcept { return !(*this == other); }
};

/// \brief Complete RFC 0007 reference origin with region and activation.
///
/// `ReferenceOrigin.active` must be `Static`, `Input`, or `Loan`.
struct ReferenceOrigin final {
  ReferenceRoot root;
  RegionKey active;
  MirEventKey activation;

  ZC_NODISCARD ReferenceOrigin clone() const {
    return ReferenceOrigin{root.clone(), active.clone(), activation};
  }

  bool operator==(const ReferenceOrigin& other) const noexcept {
    return root == other.root && active == other.active && activation == other.activation;
  }
  bool operator!=(const ReferenceOrigin& other) const noexcept { return !(*this == other); }
};

}  // namespace zomlang::compiler::ownership::facts
