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
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/key/crate-key.h"
#include "zomlang/compiler/identity/key/source-key.h"
#include "zomlang/compiler/source/core-source-admission.h"

namespace zomlang::compiler::source::core {

/// \brief One stable logical core module and its admitted source identity.
class AdmittedCoreSourceCatalogEntry final {
public:
  ZC_NODISCARD static zc::Maybe<AdmittedCoreSourceCatalogEntry> from(
      zc::Vector<identity::ModulePathSegment>&& module, identity::SourceFileKey&& source,
      const identity::Sha256Digest& contentDigest);

  ~AdmittedCoreSourceCatalogEntry() noexcept(false);
  AdmittedCoreSourceCatalogEntry(AdmittedCoreSourceCatalogEntry&&) noexcept;
  AdmittedCoreSourceCatalogEntry& operator=(AdmittedCoreSourceCatalogEntry&&) noexcept;
  ZC_DISALLOW_COPY(AdmittedCoreSourceCatalogEntry);

  ZC_NODISCARD AdmittedCoreSourceCatalogEntry clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModulePathSegment> module() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;

private:
  struct Impl;
  explicit AdmittedCoreSourceCatalogEntry(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Session-owned structural core catalog containing no semantic handles.
class AdmittedCoreSourceCatalog final {
public:
  ~AdmittedCoreSourceCatalog() noexcept(false);
  AdmittedCoreSourceCatalog(AdmittedCoreSourceCatalog&&) noexcept;
  AdmittedCoreSourceCatalog& operator=(AdmittedCoreSourceCatalog&&) noexcept;
  ZC_DISALLOW_COPY(AdmittedCoreSourceCatalog);

  ZC_NODISCARD const identity::CrateKey& crate() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& distribution() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const AdmittedCoreSourceCatalogEntry> entries() const noexcept;
  ZC_NODISCARD zc::Maybe<const AdmittedCoreSourceCatalogEntry&> find(
      zc::ArrayPtr<const identity::ModulePathSegment> module) const noexcept;

private:
  struct Impl;
  explicit AdmittedCoreSourceCatalog(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class CoreSourceCatalogAdmission;
};

using CoreSourceCatalogAdmissionResult =
    zc::OneOf<AdmittedCoreSourceCatalog, CoreDistributionAdmissionFailure>;

/// \brief Constructs and independently verifies one handle-free structural core catalog.
class CoreSourceCatalogAdmission final {
public:
  ZC_NODISCARD static CoreSourceCatalogAdmissionResult admit(
      const VerifiedCoreDistribution& distribution, const identity::CrateKey& crate);
};

}  // namespace zomlang::compiler::source::core
