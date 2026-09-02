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

#include "compiler/identity/key/package-key.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::driver::package {

/// \brief Immutable canonical inventory of admitted regular package files.
class PackageSourceInventory final {
public:
  /// \brief Sorts regular-file paths by canonical bytes and rejects duplicates.
  ZC_NODISCARD static zc::Maybe<PackageSourceInventory> from(
      zc::Vector<identity::CanonicalRelativePath>&& regularFiles);

  /// \brief Walks a package directory tree and collects its regular files.
  ///
  /// Recurses through `root`, mapping each regular file's path components to
  /// canonical path segments. A file whose path contains a non-canonical
  /// component is silently skipped, not admitted, so the inventory tolerates
  /// files outside the canonical namespace rather than rejecting the package.
  ///
  /// This lenient skip differs deliberately from `admittedEntries` in
  /// `source-snapshot.cc`, which fails closed on a non-canonical entry and also
  /// rejects duplicate, Unicode-collision, and case-fold-collision names. The two
  /// serve different roles: this walk lists source-relative paths for manifest
  /// processing, while `admittedEntries` materializes a collision-safe verified
  /// snapshot. Unifying the two is a separate semantic decision, not part of this
  /// listing.
  ///
  /// \param root The package root directory to walk.
  /// \return The sorted, duplicate-free inventory, or none when `from` rejects
  ///         the collected paths.
  ZC_NODISCARD static zc::Maybe<PackageSourceInventory> walk(const zc::ReadableDirectory& root);

  PackageSourceInventory(PackageSourceInventory&&) noexcept = default;
  PackageSourceInventory& operator=(PackageSourceInventory&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageSourceInventory);

  ZC_NODISCARD PackageSourceInventory clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalRelativePath> regularFiles() const noexcept;
  ZC_NODISCARD bool containsRegularFile(const identity::CanonicalRelativePath& path) const;

private:
  explicit PackageSourceInventory(
      zc::Vector<identity::CanonicalRelativePath>&& regularFiles) noexcept;

  zc::Vector<identity::CanonicalRelativePath> regularFileValues;
};

}  // namespace zomlang::compiler::driver::package
