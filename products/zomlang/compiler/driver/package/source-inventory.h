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
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/key/package-key.h"

namespace zomlang::compiler::driver::package {

/// \brief Immutable canonical inventory of admitted regular package files.
class PackageSourceInventory final {
public:
  /// \brief Sorts regular-file paths by canonical bytes and rejects duplicates.
  ZC_NODISCARD static zc::Maybe<PackageSourceInventory> from(
      zc::Vector<identity::CanonicalRelativePath>&& regularFiles);

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
