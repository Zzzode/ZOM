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

#include "zomlang/compiler/ownership/ownership-proof-validation.h"

namespace zomlang::compiler::ownership {

struct ValidatedOwnershipProofs::Impl final {
  Impl(facts::VerifiedOwnershipInputs&& inputs,
       facts::VerifiedRegionMemberships&& regionMemberships,
       OwnershipProofValidationReport report) noexcept
      : inputs(zc::mv(inputs)),
        regionMemberships(zc::mv(regionMemberships)),
        report(report) {}

  facts::VerifiedOwnershipInputs inputs;
  facts::VerifiedRegionMemberships regionMemberships;
  OwnershipProofValidationReport report;
};

ValidatedOwnershipProofs::ValidatedOwnershipProofs(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
ValidatedOwnershipProofs::~ValidatedOwnershipProofs() noexcept(false) = default;
ValidatedOwnershipProofs::ValidatedOwnershipProofs(ValidatedOwnershipProofs&&) noexcept = default;
ValidatedOwnershipProofs& ValidatedOwnershipProofs::operator=(ValidatedOwnershipProofs&&) noexcept =
    default;

const facts::VerifiedEscapeFacts& ValidatedOwnershipProofs::escapes() const noexcept {
  return impl->inputs.escapes();
}

const facts::VerifiedRegionMemberships& ValidatedOwnershipProofs::regionMemberships() const noexcept {
  return impl->regionMemberships;
}

const facts::VerifiedCaptureFacts& ValidatedOwnershipProofs::captures() const noexcept {
  return impl->inputs.captures();
}

const OwnershipProofValidationReport& ValidatedOwnershipProofs::report() const noexcept {
  return impl->report;
}

facts::VerifiedOwnershipInputs ValidatedOwnershipProofs::takeInputs() && noexcept {
  return zc::mv(impl->inputs);
}

ir::IrOperationResult<ValidatedOwnershipProofs> OwnershipProofValidation::validate(
    facts::VerifiedOwnershipInputs&& inputs,
    facts::VerifiedRegionMemberships&& regionMemberships) {
  // Deferred RFC 0013 cross-checks. These await inputs the admitted subset does
  // not yet produce, so implementing them now would add unreachable, untestable
  // branches:
  //   - escape<->region-outlives orientation and Contained required-point-set
  //     containment need Store or ClosureCapture escapes carrying a destination
  //     or closure region; the admitted subset emits only Return escapes.
  //   - DirectInput proof-to-borrow-input matching needs a validate() signature
  //     that exposes the resolved borrow evidence input set.
  // Conflicting-borrow and use-after-move rejection are not listed here: they
  // are performed on the production path by BorrowSourceVerifier and
  // InitializationSourceVerifier respectively, before this validation runs.
  OwnershipProofValidationReport report;
  report.validatedEscapeProofs = inputs.escapes().escapes().size();
  report.validatedRegionMemberships = regionMemberships.memberships().size();
  report.validatedCaptureFacts = inputs.captures().captures().size();
  auto validated = ValidatedOwnershipProofs(zc::heap<ValidatedOwnershipProofs::Impl>(
      zc::mv(inputs), zc::mv(regionMemberships), report));
  return ir::IrOperationResult<ValidatedOwnershipProofs>::verified(zc::mv(validated));
}

}  // namespace zomlang::compiler::ownership
