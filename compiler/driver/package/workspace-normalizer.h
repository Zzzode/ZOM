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

#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "compiler/driver/package/manifest-parser.h"

namespace zomlang::compiler::driver::package {

/// \brief One available workspace-member manifest and its package-root inventory.
class WorkspaceMemberInput final {
public:
  ZC_NODISCARD static WorkspaceMemberInput from(
      identity::CanonicalWorkspaceRelativePath&& packageDirectory, zc::String&& manifestSource,
      PackageSourceInventory&& inventory);

  WorkspaceMemberInput(WorkspaceMemberInput&&) noexcept = default;
  WorkspaceMemberInput& operator=(WorkspaceMemberInput&&) noexcept = default;
  ZC_DISALLOW_COPY(WorkspaceMemberInput);

  ZC_NODISCARD const identity::CanonicalWorkspaceRelativePath& packageDirectory() const noexcept;
  ZC_NODISCARD zc::StringPtr manifestSource() const noexcept;
  ZC_NODISCARD const PackageSourceInventory& inventory() const noexcept;

private:
  WorkspaceMemberInput(identity::CanonicalWorkspaceRelativePath&& packageDirectory,
                       zc::String&& manifestSource, PackageSourceInventory&& inventory) noexcept;

  identity::CanonicalWorkspaceRelativePath packageDirectoryValue;
  zc::String manifestSourceValue;
  PackageSourceInventory inventoryValue;
};

/// \brief One normalized non-root workspace package in canonical directory order.
class NormalizedWorkspaceMember final {
public:
  NormalizedWorkspaceMember(NormalizedWorkspaceMember&&) noexcept = default;
  NormalizedWorkspaceMember& operator=(NormalizedWorkspaceMember&&) noexcept = default;
  ZC_DISALLOW_COPY(NormalizedWorkspaceMember);

  ZC_NODISCARD const identity::CanonicalWorkspaceRelativePath& packageDirectory() const noexcept;
  ZC_NODISCARD const NormalizedManifest& manifest() const noexcept;

private:
  NormalizedWorkspaceMember(identity::CanonicalWorkspaceRelativePath&& packageDirectory,
                            NormalizedManifest&& manifest) noexcept;

  identity::CanonicalWorkspaceRelativePath packageDirectoryValue;
  NormalizedManifest manifestValue;

  friend class NormalizedWorkspace;
  friend zc::OneOf<class NormalizedWorkspace, ManifestFailure> normalizeWorkspace(
      zc::StringPtr, const PackageSourceInventory&, zc::Vector<WorkspaceMemberInput>&&);
};

/// \brief Expanded root plus every explicit member in canonical directory order.
class NormalizedWorkspace final {
public:
  NormalizedWorkspace(NormalizedWorkspace&&) noexcept = default;
  NormalizedWorkspace& operator=(NormalizedWorkspace&&) noexcept = default;
  ZC_DISALLOW_COPY(NormalizedWorkspace);

  ZC_NODISCARD const NormalizedManifest& root() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const NormalizedWorkspaceMember> members() const noexcept;

private:
  NormalizedWorkspace(NormalizedManifest&& root,
                      zc::Vector<NormalizedWorkspaceMember>&& members) noexcept;

  NormalizedManifest rootValue;
  zc::Vector<NormalizedWorkspaceMember> memberValues;

  friend zc::OneOf<NormalizedWorkspace, ManifestFailure> normalizeWorkspace(
      zc::StringPtr, const PackageSourceInventory&, zc::Vector<WorkspaceMemberInput>&&);
};

using WorkspaceNormalizeResult = zc::OneOf<NormalizedWorkspace, ManifestFailure>;

/// \brief Expands and validates one local workspace from explicitly supplied member inputs.
ZC_NODISCARD WorkspaceNormalizeResult
normalizeWorkspace(zc::StringPtr rootManifestSource, const PackageSourceInventory& rootInventory,
                   zc::Vector<WorkspaceMemberInput>&& availableMembers);

}  // namespace zomlang::compiler::driver::package
