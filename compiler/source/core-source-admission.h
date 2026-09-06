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

#include "compiler/driver/package/source-snapshot.h"
#include "compiler/source/core-distribution.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::source::core {

/// \brief Closed operational failures that can prevent core distribution admission.
enum class CoreDistributionAdmissionFailureKind : uint8_t {
  ReadFailed = 0x01,
  InvalidPath = 0x02,
  InvalidSourceBytes = 0x03,
  ResourceLimitExceeded = 0x04,
  SourceChangedDuringAdmission = 0x05,
  SourceIntegrityMismatch = 0x06,
  DistributionMismatch = 0x07,
  EditionMismatch = 0x08,
};

/// \brief Closed invariant failures detected while admitting a core distribution.
enum class CoreDistributionAdmissionInvariantKind : uint8_t {
  InputContextMismatch = 0x01,
  VerifiedStateMismatch = 0x02,
  VerifierDisagreement = 0x03,
};

/// \brief Source-distribution failure before one semantic context is published.
class CoreDistributionAdmissionFailure final {
public:
  ZC_NODISCARD static CoreDistributionAdmissionFailure withoutCoordinate(
      CoreDistributionAdmissionFailureKind kind);
  ZC_NODISCARD static CoreDistributionAdmissionFailure inventoryEntry(uint64_t ordinal);
  ZC_NODISCARD static CoreDistributionAdmissionFailure file(
      CoreDistributionAdmissionFailureKind kind, identity::CanonicalRelativePath&& path);

  CoreDistributionAdmissionFailure(CoreDistributionAdmissionFailure&&) noexcept = default;
  CoreDistributionAdmissionFailure& operator=(CoreDistributionAdmissionFailure&&) noexcept =
      default;
  ZC_DISALLOW_COPY(CoreDistributionAdmissionFailure);

  ZC_NODISCARD CoreDistributionAdmissionFailureKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<uint64_t> inventoryOrdinal() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::CanonicalRelativePath&> path() const noexcept;

private:
  CoreDistributionAdmissionFailure(CoreDistributionAdmissionFailureKind kind,
                                   zc::Maybe<uint64_t> inventoryOrdinal,
                                   zc::Maybe<identity::CanonicalRelativePath>&& path) noexcept;

  CoreDistributionAdmissionFailureKind kindValue;
  zc::Maybe<uint64_t> inventoryOrdinalValue;
  zc::Maybe<identity::CanonicalRelativePath> pathValue;
};

/// \brief Opaque process-local proof that one executable-relative core root was admitted.
class VerifiedCoreSourceRoot final {
public:
  ~VerifiedCoreSourceRoot() noexcept(false);
  VerifiedCoreSourceRoot(VerifiedCoreSourceRoot&&) noexcept;
  VerifiedCoreSourceRoot& operator=(VerifiedCoreSourceRoot&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreSourceRoot);

private:
  struct Impl;
  explicit VerifiedCoreSourceRoot(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class CoreDistributionAdmission;
};

/// \brief Immutable admitted bytes for one canonical core source file.
class VerifiedCoreSourceSnapshot final {
public:
  ZC_NODISCARD static zc::Maybe<VerifiedCoreSourceSnapshot> from(
      identity::CanonicalRelativePath&& path, zc::Array<uint8_t>&& bytes);

  ~VerifiedCoreSourceSnapshot() noexcept(false);
  VerifiedCoreSourceSnapshot(VerifiedCoreSourceSnapshot&&) noexcept;
  VerifiedCoreSourceSnapshot& operator=(VerifiedCoreSourceSnapshot&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreSourceSnapshot);

  ZC_NODISCARD VerifiedCoreSourceSnapshot clone() const;
  ZC_NODISCARD const identity::CanonicalRelativePath& path() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;

private:
  struct Impl;
  explicit VerifiedCoreSourceSnapshot(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Verified source-backed core distribution with process-local provenance.
class VerifiedCoreDistribution final {
public:
  ~VerifiedCoreDistribution() noexcept(false);
  VerifiedCoreDistribution(VerifiedCoreDistribution&&) noexcept;
  VerifiedCoreDistribution& operator=(VerifiedCoreDistribution&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreDistribution);

  ZC_NODISCARD const CoreDistributionRecord& record() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& distributionDigest() const noexcept;
  ZC_NODISCARD const CoreStandardMarkerPolicyTemplate& policyTemplate() const noexcept;
  ZC_NODISCARD const VerifiedCoreSourceRoot& sourceRoot() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedCoreSourceSnapshot> snapshots() const noexcept;

private:
  struct Impl;
  explicit VerifiedCoreDistribution(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class CoreDistributionAdmission;
};

using CoreDistributionAdmissionResult =
    zc::OneOf<VerifiedCoreDistribution, CoreDistributionAdmissionFailure,
              CoreDistributionAdmissionInvariantKind>;

/// \brief Returns the stable CLI reason for one operational admission failure.
ZC_NODISCARD zc::StringPtr coreDistributionAdmissionFailureDisplay(
    CoreDistributionAdmissionFailureKind kind) noexcept;

/// \brief Admits a fixed source tree against compiler-embedded distribution authority.
class CoreDistributionAdmission final {
public:
  explicit CoreDistributionAdmission(driver::package::SourceAdmissionLimits limits = {});

  CoreDistributionAdmission(CoreDistributionAdmission&&) noexcept = default;
  CoreDistributionAdmission& operator=(CoreDistributionAdmission&&) noexcept = default;
  ZC_DISALLOW_COPY(CoreDistributionAdmission);

  /// \brief Builds and independently verifies one immutable core distribution.
  /// \param sourceRoot Executable-relative build or installed source root selected by the caller.
  /// \param snapshotFactory Private materialization authority for immutable source bytes.
  /// \param expected Compiler-embedded accepted distribution record, digest, and policy template.
  /// \param projectedEditionYear Edition of the finalized projected core crate.
  ZC_NODISCARD CoreDistributionAdmissionResult
  admit(const zc::ReadableDirectory& sourceRoot,
        driver::package::FreshSourceDirectoryFactory& snapshotFactory,
        const CoreDistributionInputRecord& expected, uint32_t projectedEditionYear) const;

private:
  driver::package::SourceAdmissionLimits limits;
};

}  // namespace zomlang::compiler::source::core
