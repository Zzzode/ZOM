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
#include "zomlang/compiler/ownership/ownership-source-failure.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Successful linear-obligation source validation over verified resource facts.
struct LinearSourceAccepted final {};

/// \brief Ownership-specific source result for the linear-obligation precheck.
///
/// This result is deliberately separate from RFC 0010 feature-boundary results:
/// ownership source rejections are inputs to RFC 0013 ownership analysis and
/// are legal only at ownership proof validation. The result can be constructed
/// only by `OwnershipResourceVerifier`; every other producer must go through
/// the verifier's independent reconstruction.
class LinearSourceVerificationResult final {
public:
  using SourceFailures =
      ir::SortedSourceFailureFacts<OwnershipSourceFailure, OwnershipSourceFailureOrdering>;

  LinearSourceVerificationResult(LinearSourceVerificationResult&&) noexcept = default;
  LinearSourceVerificationResult& operator=(LinearSourceVerificationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(LinearSourceVerificationResult);

  ZC_NODISCARD bool isVerified() const noexcept { return value.is<Verified>(); }
  ZC_NODISCARD bool isSourceRejected() const noexcept { return value.is<SourceRejected>(); }
  ZC_NODISCARD bool isIdentityInvariantRejected() const noexcept {
    return value.is<ir::IdentityInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD bool isIrInvariantRejected() const noexcept {
    return value.is<ir::IrInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD LinearSourceAccepted&& takeVerified() && {
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
    LinearSourceAccepted value;
  };
  struct SourceRejected final {
    SourceFailures failures;
  };

  ZC_NODISCARD static LinearSourceVerificationResult verified(
      LinearSourceAccepted&& value) noexcept {
    return LinearSourceVerificationResult(Verified{zc::mv(value)});
  }
  ZC_NODISCARD static LinearSourceVerificationResult sourceRejected(
      SourceFailures&& failures) noexcept {
    return LinearSourceVerificationResult(SourceRejected{zc::mv(failures)});
  }
  ZC_NODISCARD static LinearSourceVerificationResult identityInvariantRejected(
      ir::SortedIdentityInvariantFacts&& failures) noexcept {
    return LinearSourceVerificationResult(
        ir::IdentityInvariantRejectedIrOperation{zc::mv(failures)});
  }
  ZC_NODISCARD static LinearSourceVerificationResult irInvariantRejected(
      ir::SortedIrInvariantFailureFacts&& failures) noexcept {
    return LinearSourceVerificationResult(ir::IrInvariantRejectedIrOperation{zc::mv(failures)});
  }

  explicit LinearSourceVerificationResult(Verified&& result) noexcept : value(zc::mv(result)) {}
  explicit LinearSourceVerificationResult(SourceRejected&& result) noexcept
      : value(zc::mv(result)) {}
  explicit LinearSourceVerificationResult(
      ir::IdentityInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}
  explicit LinearSourceVerificationResult(ir::IrInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}

  zc::OneOf<Verified, SourceRejected, ir::IdentityInvariantRejectedIrOperation,
            ir::IrInvariantRejectedIrOperation>
      value;

  friend class OwnershipResourceVerifier;
};

}  // namespace zomlang::compiler::ownership::facts
