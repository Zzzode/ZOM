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

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ir/ir-failure.h"
#include "zomlang/compiler/ownership/facts/loans.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/facts/refs.h"
#include "zomlang/compiler/ownership/ownership-source-failure.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Successful borrow-source validation over verified loan and reference facts.
struct BorrowSourceAccepted final {};

/// \brief Ownership-specific source result for the borrow conflict and escape precheck.
///
/// This result is deliberately separate from RFC 0010 feature-boundary results:
/// ownership source rejections are inputs to RFC 0013 ownership analysis and
/// are legal only at ownership proof validation. The result can be constructed
/// only by `BorrowSourceVerifier`; every other producer must go through the
/// verifier's independent reconstruction.
class BorrowSourceVerificationResult final {
public:
  using SourceFailures =
      ir::SortedSourceFailureFacts<OwnershipSourceFailure, OwnershipSourceFailureOrdering>;

  BorrowSourceVerificationResult(BorrowSourceVerificationResult&&) noexcept = default;
  BorrowSourceVerificationResult& operator=(BorrowSourceVerificationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(BorrowSourceVerificationResult);

  ZC_NODISCARD bool isVerified() const noexcept { return value.is<Verified>(); }
  ZC_NODISCARD bool isSourceRejected() const noexcept { return value.is<SourceRejected>(); }
  ZC_NODISCARD bool isIdentityInvariantRejected() const noexcept {
    return value.is<ir::IdentityInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD bool isIrInvariantRejected() const noexcept {
    return value.is<ir::IrInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD BorrowSourceAccepted&& takeVerified() && {
    return zc::mv(value.get<Verified>().value);
  }
  ZC_NODISCARD SourceFailures&& takeSourceFailures() && {
    return zc::mv(value.get<SourceRejected>().failures);
  }
  ZC_NODISCARD ir::SortedIdentityInvariantFacts&& takeIdentityFailures() && {
    return zc::mv(value.get<ir::IdentityInvariantRejectedIrOperation>().failures);
  }
  ZC_NODISCARD ir::SortedIrInvariantFailureFacts&& takeInvariantFailures() && {
    return zc::mv(value.get<ir::IrInvariantRejectedIrOperation>().failures);
  }

private:
  struct Verified final {
    BorrowSourceAccepted value;
  };
  struct SourceRejected final {
    SourceFailures failures;
  };

  ZC_NODISCARD static BorrowSourceVerificationResult verified(BorrowSourceAccepted&& value) noexcept {
    return BorrowSourceVerificationResult(Verified{zc::mv(value)});
  }
  ZC_NODISCARD static BorrowSourceVerificationResult sourceRejected(
      SourceFailures&& failures) noexcept {
    return BorrowSourceVerificationResult(SourceRejected{zc::mv(failures)});
  }
  ZC_NODISCARD static BorrowSourceVerificationResult identityInvariantRejected(
      ir::SortedIdentityInvariantFacts&& failures) noexcept {
    return BorrowSourceVerificationResult(
        ir::IdentityInvariantRejectedIrOperation{zc::mv(failures)});
  }
  ZC_NODISCARD static BorrowSourceVerificationResult irInvariantRejected(
      ir::SortedIrInvariantFailureFacts&& failures) noexcept {
    return BorrowSourceVerificationResult(ir::IrInvariantRejectedIrOperation{zc::mv(failures)});
  }

  explicit BorrowSourceVerificationResult(Verified&& result) noexcept : value(zc::mv(result)) {}
  explicit BorrowSourceVerificationResult(SourceRejected&& result) noexcept
      : value(zc::mv(result)) {}
  explicit BorrowSourceVerificationResult(
      ir::IdentityInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}
  explicit BorrowSourceVerificationResult(ir::IrInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}

  zc::OneOf<Verified, SourceRejected, ir::IdentityInvariantRejectedIrOperation,
            ir::IrInvariantRejectedIrOperation>
      value;

  friend class BorrowSourceVerifier;
};

/// \brief Independently validates borrow conflicts, move-out-of-borrow, and local-borrow escape.
///
/// The verifier runs after the loan and reference inventories are independently
/// verified. It derives each loan's event-granular liveness region from the last
/// use of its destination temporary, then:
///
/// - emits `MutableBorrowConflictFailure` for a mutable borrow whose place
///   carries an active overlapping loan;
/// - emits `SharedBorrowConflictFailure` for a shared borrow whose place
///   carries an active mutable loan;
/// - emits `MoveOutOfBorrowFailure` for a `Move` operand whose place carries
///   an active overlapping loan;
/// - emits `BorrowDoesNotLiveLongEnoughFailure` for a returned reference whose
///   origin is a function-local binding, because the local storage does not
///   outlive the return.
///
/// Suppression rules 5 and 6 (a rejected borrow issues no loan, a rejected
/// escape extends no provenance) are producer invariants: the verifier emits
/// each primary exactly once per conflicting loan or escaping definition.
class BorrowSourceVerifier final {
public:
  ZC_NODISCARD static BorrowSourceVerificationResult verify(
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
      const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
      const VerifiedReferenceDefinitions& references);

private:
  ZC_NODISCARD static BorrowSourceVerificationResult reject(
      const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities,
      ir::IrFailureKind kind, uint32_t ordinal);
};

}  // namespace zomlang::compiler::ownership::facts
