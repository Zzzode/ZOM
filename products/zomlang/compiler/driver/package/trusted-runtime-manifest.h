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
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/package/build-script-runtime.h"

namespace zomlang::compiler::driver::package {

enum class TrustedRuntimeManifestKind : uint8_t {
  Symbols = 0x01,
  Relocations = 0x02,
  Operations = 0x03,
};

/// \brief Hashes already encoded records with the exact versioned manifest framing.
ZC_NODISCARD identity::Sha256Digest digestTrustedRuntimeManifestFrames(
    TrustedRuntimeManifestKind kind, zc::ArrayPtr<const zc::Array<uint8_t>> records);

enum class TrustedRuntimeSymbolKindTag : uint8_t {
  NoType = 0x01,
  Object = 0x02,
  Function = 0x03,
  Section = 0x04,
  File = 0x05,
  Common = 0x06,
  Tls = 0x07,
  OsSpecific = 0x08,
  ProcessorSpecific = 0x09,
};

enum class TrustedRuntimeSymbolBindingTag : uint8_t {
  Local = 0x01,
  Global = 0x02,
  Weak = 0x03,
  OsSpecific = 0x04,
  ProcessorSpecific = 0x05,
};

enum class TrustedRuntimeSymbolVisibility : uint8_t {
  Default = 0x01,
  Internal = 0x02,
  Hidden = 0x03,
  Protected = 0x04,
};

enum class TrustedRuntimeSymbolSectionTag : uint8_t {
  Undefined = 0x01,
  Absolute = 0x02,
  Common = 0x03,
  Section = 0x04,
};

enum class TrustedRuntimeSymbolNameTag : uint8_t {
  Unnamed = 0x01,
  Named = 0x02,
};

/// \brief Encodes the closed trusted-runtime symbol-name union.
/// \param tag Closed union discriminator.
/// \param name Name payload, which must be empty for Unnamed and valid for Named.
/// \return Canonical tagged bytes or the exact invariant failure.
ZC_NODISCARD zc::OneOf<zc::Array<uint8_t>, TrustedRuntimeInvariantIssue>
encodeTrustedRuntimeSymbolName(TrustedRuntimeSymbolNameTag tag, zc::ArrayPtr<const uint8_t> name);

/// \brief Hashes a symbol name with the independent versioned codec domain.
/// \param tag Closed union discriminator.
/// \param name Name payload, which must be empty for Unnamed and valid for Named.
/// \return Domain-separated digest or the exact invariant failure.
ZC_NODISCARD zc::OneOf<identity::Sha256Digest, TrustedRuntimeInvariantIssue>
digestTrustedRuntimeSymbolName(TrustedRuntimeSymbolNameTag tag, zc::ArrayPtr<const uint8_t> name);

struct TrustedRuntimeSymbolId final {
  uint32_t objectOrdinal;
  uint32_t symbolTableSectionOrdinal;
  uint32_t symbolIndex;

  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD bool operator==(const TrustedRuntimeSymbolId& other) const noexcept;
};

/// \brief One lossless decoded ELF symbol record before trusted-manifest admission.
class TrustedRuntimeSymbolRecord final {
public:
  ZC_NODISCARD static TrustedRuntimeSymbolRecord unnamed(
      TrustedRuntimeSymbolId id, TrustedRuntimeSymbolKindTag kind, uint8_t nativeKind,
      TrustedRuntimeSymbolBindingTag binding, uint8_t nativeBinding,
      TrustedRuntimeSymbolVisibility visibility, TrustedRuntimeSymbolSectionTag section,
      uint32_t sectionOrdinal, uint64_t byteSize);
  ZC_NODISCARD static TrustedRuntimeSymbolRecord named(
      TrustedRuntimeSymbolId id, zc::Array<uint8_t>&& name, TrustedRuntimeSymbolKindTag kind,
      uint8_t nativeKind, TrustedRuntimeSymbolBindingTag binding, uint8_t nativeBinding,
      TrustedRuntimeSymbolVisibility visibility, TrustedRuntimeSymbolSectionTag section,
      uint32_t sectionOrdinal, uint64_t byteSize);
  TrustedRuntimeSymbolRecord(TrustedRuntimeSymbolRecord&&) noexcept = default;
  TrustedRuntimeSymbolRecord& operator=(TrustedRuntimeSymbolRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(TrustedRuntimeSymbolRecord);

  ZC_NODISCARD TrustedRuntimeSymbolRecord clone() const;
  ZC_NODISCARD const TrustedRuntimeSymbolId& id() const noexcept;
  ZC_NODISCARD bool isNamed() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> name() const noexcept;
  ZC_NODISCARD TrustedRuntimeSymbolKindTag kind() const noexcept;
  ZC_NODISCARD TrustedRuntimeSymbolBindingTag binding() const noexcept;
  ZC_NODISCARD TrustedRuntimeSymbolSectionTag section() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  friend class TrustedRuntimeManifestSet;
  TrustedRuntimeSymbolRecord(TrustedRuntimeSymbolId id, bool named, zc::Array<uint8_t>&& name,
                             TrustedRuntimeSymbolKindTag kind, uint8_t nativeKind,
                             TrustedRuntimeSymbolBindingTag binding, uint8_t nativeBinding,
                             TrustedRuntimeSymbolVisibility visibility,
                             TrustedRuntimeSymbolSectionTag section, uint32_t sectionOrdinal,
                             uint64_t byteSize) noexcept;
  TrustedRuntimeSymbolId idValue;
  bool namedValue;
  zc::Array<uint8_t> nameValue;
  TrustedRuntimeSymbolKindTag kindValue;
  uint8_t nativeKindValue;
  TrustedRuntimeSymbolBindingTag bindingValue;
  uint8_t nativeBindingValue;
  TrustedRuntimeSymbolVisibility visibilityValue;
  TrustedRuntimeSymbolSectionTag sectionValue;
  uint32_t sectionOrdinalValue;
  uint64_t byteSizeValue;
};

struct TrustedRuntimeRelocationRecord final {
  uint32_t objectOrdinal;
  uint32_t sectionOrdinal;
  uint64_t byteOffset;
  uint32_t kind;
  TrustedRuntimeSymbolId target;
  int64_t addend;

  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
};

enum class TrustedRuntimeOperation : uint8_t {
  Allocate = 0x01,
  Deallocate = 0x02,
  ReadRequestFrame = 0x03,
  WriteResponseFrame = 0x04,
  ValidateContractPath = 0x05,
  ReadInput = 0x06,
  ReadEnvironment = 0x07,
  WriteOutput = 0x08,
  ExportEnvironment = 0x09,
  OpenInput = 0x0a,
  OpenOutput = 0x0b,
  ReadFile = 0x0c,
  WriteFile = 0x0d,
  CloseFile = 0x0e,
  Fail = 0x0f,
  Exit = 0x10,
};

struct TrustedRuntimeOperationRecord final {
  TrustedRuntimeOperation operation;
  TrustedRuntimeSymbolId symbol;

  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
};

/// \brief Canonically sorted and structurally verified trusted-runtime manifests.
class TrustedRuntimeManifestSet final {
public:
  ZC_NODISCARD static zc::OneOf<TrustedRuntimeManifestSet, TrustedRuntimeInvariantIssue> verify(
      zc::ArrayPtr<const uint32_t> sectionCounts, zc::Vector<TrustedRuntimeSymbolRecord>&& symbols,
      zc::Vector<TrustedRuntimeRelocationRecord>&& relocations,
      zc::Vector<TrustedRuntimeOperationRecord>&& operations,
      zc::ArrayPtr<const TrustedRuntimeSymbolId> requiredOperationSymbols,
      bool hasUnexpectedInitializer = false);
  TrustedRuntimeManifestSet(TrustedRuntimeManifestSet&&) noexcept = default;
  TrustedRuntimeManifestSet& operator=(TrustedRuntimeManifestSet&&) noexcept = default;
  ZC_DISALLOW_COPY(TrustedRuntimeManifestSet);

  ZC_NODISCARD zc::ArrayPtr<const TrustedRuntimeSymbolRecord> symbols() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const TrustedRuntimeRelocationRecord> relocations() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const TrustedRuntimeOperationRecord> operations() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& symbolDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& relocationDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& operationDigest() const noexcept;

private:
  TrustedRuntimeManifestSet(zc::Vector<TrustedRuntimeSymbolRecord>&& symbols,
                            zc::Vector<TrustedRuntimeRelocationRecord>&& relocations,
                            zc::Vector<TrustedRuntimeOperationRecord>&& operations,
                            const identity::Sha256Digest& symbolDigest,
                            const identity::Sha256Digest& relocationDigest,
                            const identity::Sha256Digest& operationDigest) noexcept;
  zc::Vector<TrustedRuntimeSymbolRecord> symbolValues;
  zc::Vector<TrustedRuntimeRelocationRecord> relocationValues;
  zc::Vector<TrustedRuntimeOperationRecord> operationValues;
  identity::Sha256Digest symbolDigestValue;
  identity::Sha256Digest relocationDigestValue;
  identity::Sha256Digest operationDigestValue;
};

/// \brief Exact declared-versus-observed evidence admitted before runtime-key construction.
class TrustedRuntimeVerificationEvidence final {
public:
  ZC_NODISCARD static zc::OneOf<TrustedRuntimeVerificationEvidence, TrustedRuntimeInvariantIssue>
  verify(zc::Vector<identity::Sha256Digest>&& declaredObjectDigests,
         TrustedRuntimeManifestSet&& declared, const TrustedRuntimeManifestSet& observed);
  TrustedRuntimeVerificationEvidence(TrustedRuntimeVerificationEvidence&&) noexcept = default;
  TrustedRuntimeVerificationEvidence& operator=(TrustedRuntimeVerificationEvidence&&) noexcept =
      default;
  ZC_DISALLOW_COPY(TrustedRuntimeVerificationEvidence);

  ZC_NODISCARD zc::ArrayPtr<const identity::Sha256Digest> objectDigests() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& symbolDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& relocationDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& operationDigest() const noexcept;

private:
  TrustedRuntimeVerificationEvidence(zc::Vector<identity::Sha256Digest>&& objectDigests,
                                     const identity::Sha256Digest& symbolDigest,
                                     const identity::Sha256Digest& relocationDigest,
                                     const identity::Sha256Digest& operationDigest) noexcept;
  zc::Vector<identity::Sha256Digest> objectDigestValues;
  identity::Sha256Digest symbolDigestValue;
  identity::Sha256Digest relocationDigestValue;
  identity::Sha256Digest operationDigestValue;
};

}  // namespace zomlang::compiler::driver::package
