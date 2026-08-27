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
#include "zomlang/compiler/identity/semantic/semantic-version.h"

namespace zomlang::compiler::identity {
class CanonicalEncoder;
}

namespace zomlang::compiler::driver::package {

/// \brief One inclusive or exclusive bound in a normalized SemVer interval.
class SemVerBound final {
public:
  /// \brief Constructs a bound whose version contains no build metadata.
  ZC_NODISCARD static zc::Maybe<SemVerBound> from(identity::ResolvedVersion&& version,
                                                  bool inclusive);

  SemVerBound(SemVerBound&&) noexcept = default;
  SemVerBound& operator=(SemVerBound&&) noexcept = default;
  ZC_DISALLOW_COPY(SemVerBound);

  ZC_NODISCARD SemVerBound clone() const;
  /// \brief Clones this bound and all owned storage into `resource`.
  ZC_NODISCARD SemVerBound clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD zc::StringPtr version() const noexcept;
  ZC_NODISCARD bool inclusive() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  SemVerBound(identity::ResolvedVersion&& version, bool inclusive) noexcept;

  identity::ResolvedVersion versionValue;
  bool inclusiveValue;
};

/// \brief Canonical major, minor, and patch bytes that admit prereleases.
class SemVerCore final {
public:
  /// \brief Extracts the three canonical core components from a resolved version.
  ZC_NODISCARD static SemVerCore from(const identity::ResolvedVersion& version);

  SemVerCore(SemVerCore&&) noexcept = default;
  SemVerCore& operator=(SemVerCore&&) noexcept = default;
  ZC_DISALLOW_COPY(SemVerCore);

  ZC_NODISCARD SemVerCore clone() const;
  /// \brief Clones this core and all owned storage into `resource`.
  ZC_NODISCARD SemVerCore clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD zc::StringPtr major() const noexcept;
  ZC_NODISCARD zc::StringPtr minor() const noexcept;
  ZC_NODISCARD zc::StringPtr patch() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  SemVerCore(zc::String&& major, zc::String&& minor, zc::String&& patch) noexcept;

  zc::String majorValue;
  zc::String minorValue;
  zc::String patchValue;
};

/// \brief One non-empty normalized SemVer interval.
class SemVerInterval final {
public:
  /// \brief Constructs an interval and rejects empty or inverted bounds.
  ZC_NODISCARD static zc::Maybe<SemVerInterval> from(zc::Maybe<SemVerBound>&& lower,
                                                     zc::Maybe<SemVerBound>&& upper);

  SemVerInterval(SemVerInterval&&) noexcept = default;
  SemVerInterval& operator=(SemVerInterval&&) noexcept = default;
  ZC_DISALLOW_COPY(SemVerInterval);

  ZC_NODISCARD SemVerInterval clone() const;
  /// \brief Clones this interval and all owned storage into `resource`.
  ZC_NODISCARD SemVerInterval clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD bool hasLower() const noexcept;
  ZC_NODISCARD bool hasUpper() const noexcept;
  /// \pre `hasLower()` is true.
  ZC_NODISCARD const SemVerBound& lower() const;
  /// \pre `hasUpper()` is true.
  ZC_NODISCARD const SemVerBound& upper() const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  SemVerInterval(zc::Maybe<SemVerBound>&& lower, zc::Maybe<SemVerBound>&& upper) noexcept;

  zc::Maybe<SemVerBound> lowerValue;
  zc::Maybe<SemVerBound> upperValue;
};

/// \brief Parsed RFC 0012 comparator intersection in canonical interval form.
class SemVerConstraint final {
public:
  /// \brief Parses the complete RFC 0012 constraint grammar without width limits.
  ZC_NODISCARD static zc::Maybe<SemVerConstraint> parse(zc::StringPtr source);
  ZC_NODISCARD static SemVerConstraint intersect(const SemVerConstraint& left,
                                                 const SemVerConstraint& right);

  SemVerConstraint(SemVerConstraint&&) noexcept = default;
  SemVerConstraint& operator=(SemVerConstraint&&) noexcept = default;
  ZC_DISALLOW_COPY(SemVerConstraint);

  ZC_NODISCARD SemVerConstraint clone() const;
  /// \brief Clones this constraint and all owned storage into `resource`.
  ZC_NODISCARD SemVerConstraint clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD zc::ArrayPtr<const SemVerInterval> intervals() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const SemVerCore> prereleaseCores() const noexcept;
  ZC_NODISCARD bool allows(const identity::ResolvedVersion& version) const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  SemVerConstraint(zc::Vector<SemVerInterval>&& intervals,
                   zc::Vector<SemVerCore>&& prereleaseCores) noexcept;

  zc::Vector<SemVerInterval> intervalValues;
  zc::Vector<SemVerCore> prereleaseCoreValues;
};

}  // namespace zomlang::compiler::driver::package
