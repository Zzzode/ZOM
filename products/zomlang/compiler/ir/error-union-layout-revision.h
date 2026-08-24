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
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "zomlang/compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::ir {

/// \brief Domain-separated immutable revision of one error-union layout descriptor.
///
/// Computed by ErrorUnionLayoutCodec over the `zom.error-union-layout` framed
/// descriptor stream and compared by digest.
class ErrorUnionLayoutRevision final {
public:
  constexpr ErrorUnionLayoutRevision() noexcept = default;

  ZC_NODISCARD static ErrorUnionLayoutRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;

  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

  bool operator==(const ErrorUnionLayoutRevision& other) const noexcept {
    return value == other.value;
  }
  bool operator!=(const ErrorUnionLayoutRevision& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit ErrorUnionLayoutRevision(const identity::Sha256Digest& digest) noexcept;

  identity::Sha256Digest value;
};

/// \brief Domain-separated immutable revision of one target-artifact ABI manifest.
///
/// Computed by ErrorUnionLayoutCodec over the `zom.target-artifact-abi` framed
/// manifest stream and compared by digest.
class TargetArtifactAbiRevision final {
public:
  constexpr TargetArtifactAbiRevision() noexcept = default;

  ZC_NODISCARD static TargetArtifactAbiRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;

  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

  bool operator==(const TargetArtifactAbiRevision& other) const noexcept {
    return value == other.value;
  }
  bool operator!=(const TargetArtifactAbiRevision& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit TargetArtifactAbiRevision(const identity::Sha256Digest& digest) noexcept;

  identity::Sha256Digest value;
};

}  // namespace zomlang::compiler::ir
