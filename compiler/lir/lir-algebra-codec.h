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
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::lir {

/// \brief Domain-separated immutable revision of one LIR lowering algebra.
///
/// Computed by LirAlgebraCodec over the `zom.lir-algebra` framed registry
/// stream and compared by digest. See RFC 0021 "LIR Algebra Registry".
class LirAlgebraRevision final {
public:
  constexpr LirAlgebraRevision() noexcept = default;

  ZC_NODISCARD static LirAlgebraRevision fromDigest(const identity::Sha256Digest& digest) noexcept;

  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return value; }

  bool operator==(const LirAlgebraRevision& other) const noexcept { return value == other.value; }
  bool operator!=(const LirAlgebraRevision& other) const noexcept { return !(*this == other); }

private:
  explicit LirAlgebraRevision(const identity::Sha256Digest& digest) noexcept : value(digest) {}

  identity::Sha256Digest value;
};

/// \brief Retained LIR lowering algebra (RFC 0021 `LirAlgebraRegistry`).
///
/// The registry retains the complete algebra, not only its digest. This
/// foundation slice models the canonical framing: the ASCII source-MIR revision
/// domain and the two framed recipe sequences (`canonicalRecipes` and
/// `canonicalGeneratedRecipes`). Each recipe is retained as its already-canonical
/// framed byte record so the codec is a total function over the registry; the
/// populated initial recipe table (the eight source recipes and eight generated
/// recipes named in RFC 0021) is the next store step. The empty/initial registry
/// reproduces the RFC's documented 56-byte oracle.
class LirAlgebraRegistry final {
public:
  LirAlgebraRegistry() = default;
  ZC_DISALLOW_COPY(LirAlgebraRegistry);
  LirAlgebraRegistry(LirAlgebraRegistry&&) = default;
  LirAlgebraRegistry& operator=(LirAlgebraRegistry&&) = default;

  /// \brief Builds a registry with the default `zom.mir-revision` source domain.
  ZC_NODISCARD static LirAlgebraRegistry empty();

  /// \brief Builds a registry with an explicit ASCII source-MIR revision domain.
  /// \return The registry, or none for an empty or non-ASCII domain.
  ZC_NODISCARD static zc::Maybe<LirAlgebraRegistry> withSourceDomain(zc::StringPtr domain);

  /// \brief Appends one canonical source recipe as an already-framed byte record.
  void addRecipe(zc::ArrayPtr<const uint8_t> canonicalRecord);
  /// \brief Appends one canonical generated recipe as an already-framed byte record.
  void addGeneratedRecipe(zc::ArrayPtr<const uint8_t> canonicalRecord);

  ZC_NODISCARD zc::StringPtr sourceMirRevisionDomain() const noexcept { return domainValue; }
  ZC_NODISCARD size_t recipeCount() const noexcept { return recipeRecords.size(); }
  ZC_NODISCARD size_t generatedRecipeCount() const noexcept { return generatedRecords.size(); }
  ZC_NODISCARD zc::ArrayPtr<const zc::Array<uint8_t>> recipes() const noexcept {
    return recipeRecords.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const zc::Array<uint8_t>> generatedRecipes() const noexcept {
    return generatedRecords.asPtr();
  }

private:
  explicit LirAlgebraRegistry(zc::String&& domain) noexcept : domainValue(zc::mv(domain)) {}

  zc::String domainValue;
  zc::Vector<zc::Array<uint8_t>> recipeRecords;
  zc::Vector<zc::Array<uint8_t>> generatedRecords;
};

/// \brief Canonical codec for the LIR lowering algebra registry.
///
/// The preimage is, per RFC 0021:
///   ASCII("zom.lir-algebra") 0x00 Frame(sourceMirRevisionDomain)
///   EncodeFramedSequence(canonicalRecipes)
///   EncodeFramedSequence(canonicalGeneratedRecipes)
/// `Frame` is a big-endian uint64 byte length followed by the exact bytes;
/// `EncodeFramedSequence` is a big-endian uint64 element count followed by one
/// `Frame` per element.
class LirAlgebraCodec final {
public:
  /// \brief Encodes the registry to its canonical preimage bytes.
  ZC_NODISCARD static zc::Array<uint8_t> encode(const LirAlgebraRegistry& registry);
  /// \brief Computes the registry's `LirAlgebraRevision` (SHA-256 of the preimage).
  ZC_NODISCARD static LirAlgebraRevision compute(const LirAlgebraRegistry& registry);
};

}  // namespace zomlang::compiler::lir
