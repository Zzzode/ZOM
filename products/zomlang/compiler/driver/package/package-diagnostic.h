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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/text/diagnostic-text.h"
#include "zomlang/compiler/driver/package/build-script-runtime.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/materialization-issue.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

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

private:
  PackageDiagnosticDocument(InputDocumentKey&& key, SanitizedSourceView&& source,
                            zc::String&& displayName) noexcept;

  InputDocumentKey keyValue;
  SanitizedSourceView sourceValue;
  zc::String displayNameValue;
};

/// \brief Returns the closed, non-secret display token for one manifest issue.
ZC_NODISCARD zc::StringPtr manifestIssueDisplay(ManifestIssue issue) noexcept;
ZC_NODISCARD zc::StringPtr materializationIssueDisplay(MaterializationIssue issue) noexcept;
ZC_NODISCARD zc::StringPtr buildScriptIssueDisplay(BuildScriptIssue issue) noexcept;
ZC_NODISCARD zc::StringPtr buildScriptLimitInvariantDisplay(
    BuildScriptLimitInvariantIssue issue) noexcept;
ZC_NODISCARD zc::StringPtr trustedRuntimeInvariantDisplay(
    TrustedRuntimeInvariantIssue issue) noexcept;

/// \brief Translates typed package failures into the compiler diagnostic engine.
class PackageDiagnosticAdapter final {
public:
  static void emitInvocationIssue(diagnostics::DiagnosticEngine& diagnostics,
                                  InvocationIssue issue);
  static void emitMaterializationIssue(diagnostics::DiagnosticEngine& diagnostics,
                                       MaterializationIssue issue);
  static void emitBuildScriptIssue(diagnostics::DiagnosticEngine& diagnostics,
                                   BuildScriptIssue issue);
  static void emitBuildScriptLimitInvariant(diagnostics::DiagnosticEngine& diagnostics,
                                            BuildScriptLimitInvariantIssue issue);
  static void emitTrustedRuntimeInvariant(diagnostics::DiagnosticEngine& diagnostics,
                                          TrustedRuntimeInvariantIssue issue);
  static bool emitManifestFailure(diagnostics::DiagnosticEngine& diagnostics,
                                  zc::ArrayPtr<const PackageDiagnosticDocument> documents,
                                  const ManifestFailure& failure);
  static bool emitToolchainModuleRootFailure(
      diagnostics::DiagnosticEngine& diagnostics,
      zc::ArrayPtr<const PackageDiagnosticDocument> documents,
      const PackageToolchainModuleRootFailure& failure);
};

}  // namespace zomlang::compiler::driver::package
