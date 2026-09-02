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

#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/driver/package/package-diagnostic.h"
#include "compiler/driver/package/workspace-normalizer.h"
#include "zc/core/filesystem.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::driver::package {

/// \brief The manifest could not be read from disk.
struct ManifestReadFailed final {};
/// \brief The package source inventory walk rejected the collected paths.
struct InventoryRejected final {};
/// \brief A manifest diagnostic document could not be constructed.
struct DiagnosticDocumentRejected final {};

/// \brief Parsing the workspace manifest failed, carrying the typed manifest
/// failure and the diagnostic documents collected so far so a caller can render
/// its provenance.
struct ManifestParseFailed final {
  ManifestFailure failure;
  zc::Vector<PackageDiagnosticDocument> diagnosticDocuments;
};

/// \brief Normalizing the workspace failed, carrying the typed manifest failure
/// and the diagnostic documents collected so far.
struct WorkspaceNormalizeFailed final {
  ManifestFailure failure;
  zc::Vector<PackageDiagnosticDocument> diagnosticDocuments;
};

/// \brief A closed set of workspace-load failures. Each alternative's payload is
/// determined by its type: the three read/inventory/document arms carry nothing,
/// while the two manifest arms carry the `ManifestFailure` their diagnostics need,
/// so a caller cannot observe a kind without its required payload.
using WorkspaceLoadFailure =
    zc::OneOf<ManifestReadFailed, InventoryRejected, DiagnosticDocumentRejected,
              ManifestParseFailed, WorkspaceNormalizeFailed>;

/// \brief A loaded and normalized workspace, plus the manifest diagnostic
/// documents retained for later diagnostics and the root directory path.
struct LoadedWorkspace final {
  NormalizedWorkspace workspace;
  zc::Path rootPath;
  zc::Vector<PackageDiagnosticDocument> diagnosticDocuments;
};

/// \brief The outcome of loading a workspace: the loaded workspace or a typed
/// failure.
using WorkspaceLoadResult = zc::OneOf<LoadedWorkspace, WorkspaceLoadFailure>;

/// \brief Locates the workspace manifest (`Zom.toml`).
///
/// With one explicit `manifestPaths` entry, validates that it names a `Zom.toml`
/// regular file; otherwise walks upward from the current directory until a
/// `Zom.toml` is found. Returns the resolved path, or an `InvocationIssue`
/// (`InvalidManifestPath` / `ManifestNotFound`) when none is admissible.
///
/// \param filesystem The filesystem to resolve against.
/// \param manifestPaths The explicit `--manifest-path` values (zero or one used).
/// \return The manifest path or the invocation issue.
ZC_NODISCARD zc::OneOf<zc::Path, InvocationIssue> discoverManifestPath(
    const zc::Filesystem& filesystem, zc::ArrayPtr<const zc::String> manifestPaths);

/// \brief Reads and normalizes the workspace rooted at `manifestPath`'s directory.
///
/// Reads the root `Zom.toml`, walks its source inventory, parses the manifest,
/// reads any workspace members, and normalizes the workspace. Every failure is
/// reported as a typed `WorkspaceLoadFailure`; the success and failure sets are
/// exactly those of the prior CLI implementation, only the failure detail is
/// richer.
///
/// \param filesystem The filesystem to read from.
/// \param manifestPath The resolved manifest path; consumed.
/// \return The loaded workspace or a typed failure.
ZC_NODISCARD WorkspaceLoadResult loadWorkspace(const zc::Filesystem& filesystem,
                                               zc::Path&& manifestPath);

}  // namespace zomlang::compiler::driver::package
