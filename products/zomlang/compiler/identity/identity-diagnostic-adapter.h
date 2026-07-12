// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/identity/identity-invariant.h"

namespace zomlang::compiler::identity {

/// \brief Validates and resolves canonical identity ranges into live source locations.
class IdentityDiagnosticLocationResolver {
public:
  virtual ~IdentityDiagnosticLocationResolver() noexcept(false) = default;
  ZC_DISALLOW_COPY(IdentityDiagnosticLocationResolver);

  ZC_NODISCARD virtual zc::Maybe<source::SourceLoc> resolve(
      const UnbrandedSourceRange& range) const = 0;

protected:
  IdentityDiagnosticLocationResolver() noexcept = default;
  IdentityDiagnosticLocationResolver(IdentityDiagnosticLocationResolver&&) noexcept = default;
  IdentityDiagnosticLocationResolver& operator=(IdentityDiagnosticLocationResolver&&) noexcept =
      default;
};

/// \brief One registered diagnostic group without a fabricated source location.
class IdentityDiagnosticGroup final {
public:
  IdentityDiagnosticGroup(IdentityDiagnosticGroup&&) noexcept = default;
  IdentityDiagnosticGroup& operator=(IdentityDiagnosticGroup&&) noexcept = default;
  ZC_DISALLOW_COPY(IdentityDiagnosticGroup);

  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD zc::Maybe<const UnbrandedSourceRange&> diagnosticRange() const;
  ZC_NODISCARD uint64_t occurrenceCount() const noexcept;

private:
  IdentityDiagnosticGroup(diagnostics::DiagID diagnosticId,
                          zc::Maybe<UnbrandedSourceRange>&& diagnosticRange,
                          uint64_t occurrenceCount) noexcept;

  diagnostics::DiagID idValue;
  zc::Maybe<UnbrandedSourceRange> rangeValue;
  uint64_t countValue;

  friend zc::Vector<IdentityDiagnosticGroup> groupIdentityInvariants(
      zc::ArrayPtr<const IdentityInvariant> facts);
};

ZC_NODISCARD diagnostics::DiagID identityDiagnosticId(IdentityInvariantKind kind);

/// \brief Groups adjacent sorted facts only by registered diagnostic and validated range.
ZC_NODISCARD zc::Vector<IdentityDiagnosticGroup> groupIdentityInvariants(
    zc::ArrayPtr<const IdentityInvariant> facts);

/// \brief Emits registered diagnostics without inventing a source location.
void emitIdentityDiagnosticGroups(
    diagnostics::DiagnosticEngine& engine, zc::ArrayPtr<const IdentityDiagnosticGroup> groups,
    zc::Maybe<const IdentityDiagnosticLocationResolver&> locationResolver = zc::none);

}  // namespace zomlang::compiler::identity
