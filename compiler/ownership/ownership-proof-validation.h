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

#include <cstddef>

#include "zc/core/memory.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ownership/facts/capture.h"
#include "compiler/ownership/facts/escape.h"
#include "compiler/ownership/facts/inputs.h"
#include "compiler/ownership/facts/region-membership.h"

namespace zomlang::compiler::ownership {

/// \brief Summary of one RFC 0013 ownership proof validation pass.
struct OwnershipProofValidationReport final {
  /// Number of escape proofs examined during the pass.
  size_t validatedEscapeProofs = 0;
  /// Number of region memberships examined during the pass.
  size_t validatedRegionMemberships = 0;
  /// Number of capture facts examined during the pass.
  size_t validatedCaptureFacts = 0;
  /// Number of non-fatal warnings recorded during the pass.
  size_t warnings = 0;
};

/// \brief Immutable RFC 0013 validated ownership proofs.
///
/// ValidatedOwnershipProofs is the sole successor of the verified ownership
/// inputs and the verified region memberships. It owns the validated escape
/// proofs, region memberships, capture facts, and the validation report. The
/// ownership finalizer consumes the embedded verified inputs through
/// takeInputs; the proof inventories remain accessible for downstream
/// diagnostics and evidence publication.
class ValidatedOwnershipProofs final {
public:
  ~ValidatedOwnershipProofs() noexcept(false);
  ValidatedOwnershipProofs(ValidatedOwnershipProofs&&) noexcept;
  ValidatedOwnershipProofs& operator=(ValidatedOwnershipProofs&&) noexcept;
  ZC_DISALLOW_COPY(ValidatedOwnershipProofs);

  /// \brief Returns the validated escape proofs.
  ZC_NODISCARD const facts::VerifiedEscapeFacts& escapes() const noexcept;
  /// \brief Returns the validated region memberships.
  ZC_NODISCARD const facts::VerifiedRegionMemberships& regionMemberships() const noexcept;
  /// \brief Returns the validated capture facts.
  ZC_NODISCARD const facts::VerifiedCaptureFacts& captures() const noexcept;
  /// \brief Returns the validation report.
  ZC_NODISCARD const OwnershipProofValidationReport& report() const noexcept;
  /// \brief Consumes the embedded verified ownership inputs for the finalizer.
  ZC_NODISCARD facts::VerifiedOwnershipInputs takeInputs() && noexcept;

private:
  struct Impl;
  explicit ValidatedOwnershipProofs(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class OwnershipProofValidation;
};

/// \brief Validates all ownership proofs before publication (RFC 0013).
///
/// The validator consumes one verified ownership input snapshot and the
/// verified region memberships, cross-checks every proof against the
/// supporting facts, and either publishes one ValidatedOwnershipProofs or
/// rejects the operation. A rejected validation destroys its consumed inputs
/// and publishes no partial successor.
class OwnershipProofValidation final {
public:
  ZC_NODISCARD static ir::IrOperationResult<ValidatedOwnershipProofs> validate(
      facts::VerifiedOwnershipInputs&& inputs,
      facts::VerifiedRegionMemberships&& regionMemberships);
};

}  // namespace zomlang::compiler::ownership
