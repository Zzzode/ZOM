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

#include "compiler/diagnostics/fact/compilation-diagnostic-facts.h"
#include "compiler/diagnostics/text/diagnostic-text.h"
#include "compiler/driver/package/build-script-runtime.h"
#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/materialization-issue.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/driver/package/verified-package-inputs.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::driver::package {

/// \brief Escaped diagnostic source with an original-byte offset projection.
class SanitizedSourceView final {
public:
  ZC_NODISCARD static SanitizedSourceView from(zc::ArrayPtr<const zc::byte> source);

  SanitizedSourceView(SanitizedSourceView&&) noexcept = default;
  SanitizedSourceView& operator=(SanitizedSourceView&&) noexcept = default;
  ZC_DISALLOW_COPY(SanitizedSourceView);

  ZC_NODISCARD zc::StringPtr escapedSource() const noexcept;
  /// \pre `originalOffset <= originalSize()`.
  ZC_NODISCARD uint64_t escapedOffset(uint64_t originalOffset) const;
  ZC_NODISCARD uint64_t originalSize() const noexcept;
  ZC_NODISCARD SanitizedSourceView clone() const;

private:
  SanitizedSourceView(zc::String&& source, zc::Vector<uint64_t>&& offsets) noexcept;

  zc::String sourceValue;
  zc::Vector<uint64_t> escapedOffsets;
};

/// \brief Verified manifest input admitted to the package diagnostic boundary.
class PackageDiagnosticDocument final {
public:
  ZC_NODISCARD static zc::Maybe<PackageDiagnosticDocument> from(
      InputDocumentKey&& key, zc::ArrayPtr<const zc::byte> source);

  PackageDiagnosticDocument(PackageDiagnosticDocument&&) noexcept = default;
  PackageDiagnosticDocument& operator=(PackageDiagnosticDocument&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageDiagnosticDocument);

  ZC_NODISCARD const InputDocumentKey& key() const noexcept;
  ZC_NODISCARD const SanitizedSourceView& sourceView() const noexcept;
  ZC_NODISCARD zc::StringPtr displayName() const noexcept;
  ZC_NODISCARD PackageDiagnosticDocument clone() const;

private:
  PackageDiagnosticDocument(InputDocumentKey&& key, SanitizedSourceView&& source,
                            zc::String&& displayName) noexcept;

  InputDocumentKey keyValue;
  SanitizedSourceView sourceValue;
  zc::String displayNameValue;
};

/// \brief Returns the closed, non-secret display token for one manifest issue.
ZC_NODISCARD zc::StringPtr manifestIssueDisplay(ManifestIssue issue) noexcept;
ZC_NODISCARD zc::StringPtr targetSelectionIssueDisplay(TargetSelectionIssue issue) noexcept;
ZC_NODISCARD zc::StringPtr materializationIssueDisplay(MaterializationIssue issue) noexcept;
ZC_NODISCARD zc::StringPtr buildScriptIssueDisplay(BuildScriptIssue issue) noexcept;
/// \brief Returns the closed, non-secret display token that locates one
/// package-input verification failure.
ZC_NODISCARD zc::StringPtr verifyFailureDisplay(const VerifyFailure& failure) noexcept;

/// \brief Canonical document-backed facts produced by one package failure.
struct PackageDiagnosticFactProjection final {
  zc::Vector<diagnostics::DiagnosticFact> facts;
  zc::Vector<diagnostics::SourceDiagnosticProvenanceEntry> provenance;
  zc::Vector<diagnostics::DiagnosticDocumentAuthority> documents;
};

enum class PackageDiagnosticProjectionFailure : uint8_t {
  InvalidProvenance = 0x01,
  MissingDocument = 0x02,
  InvalidFact = 0x03,
};

using PackageDiagnosticProjectionResult =
    zc::OneOf<PackageDiagnosticFactProjection, PackageDiagnosticProjectionFailure>;

/// \brief Projects one manifest failure without presentation side effects.
ZC_NODISCARD PackageDiagnosticProjectionResult projectManifestFailure(
    zc::ArrayPtr<const PackageDiagnosticDocument> documents, const ManifestFailure& failure);

/// \brief Projects one reserved toolchain-root failure without presentation side effects.
ZC_NODISCARD PackageDiagnosticProjectionResult
projectToolchainModuleRootFailure(zc::ArrayPtr<const PackageDiagnosticDocument> documents,
                                  const PackageToolchainModuleRootFailure& failure);

}  // namespace zomlang::compiler::driver::package
