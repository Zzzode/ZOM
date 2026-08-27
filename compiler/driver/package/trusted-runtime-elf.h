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
#include "compiler/driver/package/trusted-runtime-manifest.h"

namespace zomlang::compiler::driver::package {

enum class TrustedRuntimeElfArchitecture : uint8_t { X86_64 = 0x01, AArch64 = 0x02 };

/// \brief Complete symbol and relocation inventory decoded from one static ELF64 object.
class DecodedTrustedRuntimeElfObject final {
public:
  DecodedTrustedRuntimeElfObject(DecodedTrustedRuntimeElfObject&&) noexcept = default;
  DecodedTrustedRuntimeElfObject& operator=(DecodedTrustedRuntimeElfObject&&) noexcept = default;
  ZC_DISALLOW_COPY(DecodedTrustedRuntimeElfObject);

  ZC_NODISCARD uint32_t sectionCount() const noexcept;
  ZC_NODISCARD TrustedRuntimeElfArchitecture architecture() const noexcept;
  ZC_NODISCARD bool hasUnexpectedInitializer() const noexcept;
  ZC_NODISCARD zc::Vector<TrustedRuntimeSymbolRecord> releaseSymbols();
  ZC_NODISCARD zc::Vector<TrustedRuntimeRelocationRecord> releaseRelocations();

private:
  DecodedTrustedRuntimeElfObject(uint32_t sectionCount, TrustedRuntimeElfArchitecture architecture,
                                 zc::Vector<TrustedRuntimeSymbolRecord>&& symbols,
                                 zc::Vector<TrustedRuntimeRelocationRecord>&& relocations,
                                 bool hasUnexpectedInitializer) noexcept;
  uint32_t sectionCountValue;
  TrustedRuntimeElfArchitecture architectureValue;
  zc::Vector<TrustedRuntimeSymbolRecord> symbolValues;
  zc::Vector<TrustedRuntimeRelocationRecord> relocationValues;
  bool unexpectedInitializerValue;
  friend class TrustedRuntimeElfDecoder;
};

using TrustedRuntimeElfDecodeResult =
    zc::OneOf<DecodedTrustedRuntimeElfObject, TrustedRuntimeInvariantIssue>;

/// \brief Digest- and target-verified constructor-free static PIE build-script image.
class VerifiedBuildScriptExecutable final {
public:
  ZC_NODISCARD static zc::OneOf<VerifiedBuildScriptExecutable, BuildScriptIssue> verify(
      BuildScriptExecutableKey&& key, zc::Array<uint8_t>&& imageBytes);
  ~VerifiedBuildScriptExecutable() noexcept;
  VerifiedBuildScriptExecutable(VerifiedBuildScriptExecutable&&) noexcept;
  VerifiedBuildScriptExecutable& operator=(VerifiedBuildScriptExecutable&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBuildScriptExecutable);

  ZC_NODISCARD const BuildScriptExecutableKey& key() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept;

private:
  struct Impl;
  explicit VerifiedBuildScriptExecutable(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Bounded decoder for little-endian x86-64 and AArch64 relocatable ELF64 objects.
class TrustedRuntimeElfDecoder final {
public:
  ZC_NODISCARD static TrustedRuntimeElfDecodeResult decode(uint32_t objectOrdinal,
                                                           zc::ArrayPtr<const uint8_t> objectBytes);

  ZC_NODISCARD static zc::OneOf<TrustedRuntimeManifestSet, TrustedRuntimeInvariantIssue>
  decodeManifest(zc::ArrayPtr<const zc::Array<uint8_t>> objectBytes,
                 zc::Vector<TrustedRuntimeOperationRecord>&& operations,
                 zc::ArrayPtr<const TrustedRuntimeSymbolId> requiredOperationSymbols);

  /// \brief Decodes actual objects and admits a runtime key only after exact manifest equality.
  ZC_NODISCARD static zc::OneOf<TrustedBuildRuntimeKey, TrustedRuntimeInvariantIssue> verifyKey(
      zc::StringPtr expectedRuntimeAbi, zc::StringPtr runtimeAbi,
      zc::Vector<zc::Array<uint8_t>>&& objectBytes,
      zc::Vector<identity::Sha256Digest>&& declaredObjectDigests,
      TrustedRuntimeManifestSet&& declaredManifest,
      zc::Vector<TrustedRuntimeOperationRecord>&& observedOperations,
      zc::ArrayPtr<const TrustedRuntimeSymbolId> requiredOperationSymbols);
};

}  // namespace zomlang::compiler::driver::package
