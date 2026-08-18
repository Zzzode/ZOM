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

namespace zomlang::compiler::ownership::facts {

/// \brief Domain-separated immutable revision of one complete verified ownership facts snapshot.
///
/// The revision binds the eight independently verified facts inventories owned by
/// VerifiedOwnershipInputs together with the overlay-derived drop, unsafe, cast, and
/// marker-decision inventories and the Built MIR, event overlay, and borrow-evidence
/// lineage. It is computed by OwnershipFactsCodec and compared by digest.
class OwnershipFactsRevision final {
public:
  constexpr OwnershipFactsRevision() noexcept = default;

  ZC_NODISCARD static OwnershipFactsRevision fromDigest(const identity::Sha256Digest& digest) noexcept;

  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

  constexpr bool operator==(OwnershipFactsRevision other) const noexcept {
    return value == other.value;
  }
  constexpr bool operator!=(OwnershipFactsRevision other) const noexcept {
    return !(*this == other);
  }

private:
  explicit OwnershipFactsRevision(const identity::Sha256Digest& digest) noexcept;

  identity::Sha256Digest value;
};

}  // namespace zomlang::compiler::ownership::facts
