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
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "compiler/checker/facts/signature-facts.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "compiler/source/core-distribution.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::driver::core {

/// \brief Final core-specific authority for the standard Copy and Linear markers.
///
/// Constructed after core modules have been checked through the normal pipeline:
/// the Copy and Linear definition identities are discovered by name in the checked
/// core module interfaces, then sealed into this authority for the body checker.
class VerifiedCoreStandardMarkerAuthority final {
public:
  ~VerifiedCoreStandardMarkerAuthority() noexcept(false);
  VerifiedCoreStandardMarkerAuthority(VerifiedCoreStandardMarkerAuthority&&) noexcept;
  VerifiedCoreStandardMarkerAuthority& operator=(VerifiedCoreStandardMarkerAuthority&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreStandardMarkerAuthority);

  ZC_NODISCARD static zc::Maybe<VerifiedCoreStandardMarkerAuthority> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      identity::DefId copyDefId, identity::DefinitionKey&& copyDefKey, identity::DefId linearDefId,
      identity::DefinitionKey&& linearDefKey);

  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD identity::DefId copy() const noexcept;
  ZC_NODISCARD identity::DefId linear() const noexcept;
  ZC_NODISCARD const identity::DefinitionKey& copyKey() const noexcept;
  ZC_NODISCARD const identity::DefinitionKey& linearKey() const noexcept;

private:
  struct Impl;
  explicit VerifiedCoreStandardMarkerAuthority(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Builds the checker marker policy configuration from the core policy template
/// and the resolved Copy and Linear definition keys.
ZC_NODISCARD zc::Maybe<checker::signature::MarkerPolicyConfiguration>
buildMarkerPolicyConfiguration(const source::core::CoreStandardMarkerPolicyTemplate& policyTemplate,
                               const identity::DefinitionKey& copyKey,
                               const identity::DefinitionKey& linearKey);

}  // namespace zomlang::compiler::driver::core
