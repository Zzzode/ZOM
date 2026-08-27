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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "compiler/driver/package/source-tree.h"
#include "compiler/driver/package/zstd-decoder.h"

namespace zomlang::compiler::driver::package {

/// \brief Owned fresh private directory with an explicit cleanup operation.
class FreshSourceDirectory {
public:
  virtual ~FreshSourceDirectory() noexcept = default;
  virtual const zc::Directory& root() const = 0;
  virtual zc::Maybe<MaterializationIssue> finish() = 0;
};

using FreshSourceDirectoryResult = zc::OneOf<zc::Own<FreshSourceDirectory>, MaterializationIssue>;

/// \brief Capability that creates a new empty private source directory.
class FreshSourceDirectoryFactory {
public:
  virtual ~FreshSourceDirectoryFactory() noexcept(false) = default;
  virtual FreshSourceDirectoryResult create() = 0;
};

/// \brief Creates private replacement directories that are deleted without publication.
class ReplacementFreshSourceDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  explicit ReplacementFreshSourceDirectoryFactory(const zc::Directory& parent);
  ZC_NODISCARD FreshSourceDirectoryResult create() override;

private:
  zc::Own<const zc::Directory> parent;
};

using VerifiedFileReadResult = zc::OneOf<zc::Array<zc::byte>, MaterializationIssue>;

class DigestVerifiedSourceSnapshot;
using SourceSnapshotCopyResult =
    zc::OneOf<zc::Own<DigestVerifiedSourceSnapshot>, MaterializationIssue>;

/// \brief Owned source tree that verifies every read against its immutable inventory.
class DigestVerifiedSourceSnapshot final {
public:
  ~DigestVerifiedSourceSnapshot() noexcept;
  DigestVerifiedSourceSnapshot(DigestVerifiedSourceSnapshot&&) noexcept;
  DigestVerifiedSourceSnapshot& operator=(DigestVerifiedSourceSnapshot&&) noexcept;
  ZC_DISALLOW_COPY(DigestVerifiedSourceSnapshot);

  ZC_NODISCARD const SourceTreeRecord& record() const noexcept;
  ZC_NODISCARD VerifiedFileReadResult
  readVerifiedFile(const identity::CanonicalRelativePath& path) const;
  ZC_NODISCARD SourceSnapshotCopyResult
  materializeVerifiedCopy(FreshSourceDirectoryFactory& factory) const;
  ZC_NODISCARD zc::Maybe<MaterializationIssue> finish();

private:
  friend class SourceArchiveMaterializer;
  friend class SourceDirectoryMaterializer;

  DigestVerifiedSourceSnapshot(zc::Own<FreshSourceDirectory>&& directory,
                               SourceTreeRecord&& record);

  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Associates one immutable resolved package coordinate with its verified source tree.
class ResolvedPackageSourceSnapshot final {
public:
  ZC_NODISCARD static ResolvedPackageSourceSnapshot from(identity::PackageBaseKey&& package,
                                                         DigestVerifiedSourceSnapshot&& snapshot);

  ResolvedPackageSourceSnapshot(ResolvedPackageSourceSnapshot&&) noexcept = default;
  ResolvedPackageSourceSnapshot& operator=(ResolvedPackageSourceSnapshot&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedPackageSourceSnapshot);

  ZC_NODISCARD const identity::PackageBaseKey& package() const noexcept;
  ZC_NODISCARD const DigestVerifiedSourceSnapshot& snapshot() const noexcept;
  ZC_NODISCARD zc::Maybe<MaterializationIssue> finish();

private:
  ResolvedPackageSourceSnapshot(identity::PackageBaseKey&& package,
                                DigestVerifiedSourceSnapshot&& snapshot) noexcept;
  identity::PackageBaseKey packageValue;
  DigestVerifiedSourceSnapshot snapshotValue;
};

using SourceSnapshotResult = zc::OneOf<DigestVerifiedSourceSnapshot, MaterializationIssue>;

/// \brief Enumerates and hashes one source directory without materializing or publishing it.
/// \param source Read-only directory root scanned without following symbolic links.
/// \param limits Exact bounded admission limits applied during the scan.
/// \return One immutable canonical tree record, or the first materialization issue.
ZC_NODISCARD SourceTreeBuildResult inspectSourceDirectory(const zc::ReadableDirectory& source,
                                                          SourceAdmissionLimits limits = {});

class SourceMaterializationObserver;

/// \brief Extracts one admitted compressed archive into a fresh verified snapshot.
class SourceArchiveMaterializer final {
public:
  explicit SourceArchiveMaterializer(SourceAdmissionLimits limits = {});

  SourceArchiveMaterializer(SourceArchiveMaterializer&&) noexcept = default;
  SourceArchiveMaterializer& operator=(SourceArchiveMaterializer&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceArchiveMaterializer);

  ZC_NODISCARD SourceSnapshotResult
  materialize(ZstdInput& input, FreshSourceDirectoryFactory& factory,
              zc::Maybe<SourceMaterializationObserver&> observer = zc::none);

private:
  SourceAdmissionLimits limits;
};

/// \brief Optional synchronization point used to inject source changes between admission passes.
class SourceMaterializationObserver {
public:
  virtual ~SourceMaterializationObserver() noexcept(false) = default;
  virtual zc::Maybe<MaterializationIssue> afterFirstInventory() { return zc::none; }
  virtual zc::Maybe<MaterializationIssue> beforeDestinationCreate() { return zc::none; }
  virtual zc::Maybe<MaterializationIssue> beforeDestinationWrite() { return zc::none; }
  virtual zc::Maybe<MaterializationIssue> beforeDestinationSync() { return zc::none; }
};

/// \brief Copies a checkout or local source through two canonical inventory passes.
class SourceDirectoryMaterializer final {
public:
  explicit SourceDirectoryMaterializer(SourceAdmissionLimits limits = {});

  SourceDirectoryMaterializer(SourceDirectoryMaterializer&&) noexcept = default;
  SourceDirectoryMaterializer& operator=(SourceDirectoryMaterializer&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceDirectoryMaterializer);

  ZC_NODISCARD SourceSnapshotResult
  materialize(const zc::ReadableDirectory& source, FreshSourceDirectoryFactory& factory,
              zc::Maybe<SourceMaterializationObserver&> observer = zc::none);

private:
  SourceAdmissionLimits limits;
};

}  // namespace zomlang::compiler::driver::package
