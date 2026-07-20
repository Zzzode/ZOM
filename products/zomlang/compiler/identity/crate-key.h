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

#include "zc/core/common.h"
#include "zomlang/compiler/identity/canonical-scalar.h"
#include "zomlang/compiler/identity/package-key.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/identity/sorted-feature-set.h"

namespace zomlang::compiler::identity {

class CanonicalDecoder;
class CanonicalEncoder;

enum class Endianness : uint8_t { Little = 0x01, Big = 0x02 };

/// \brief Canonical target specification that affects semantic identity.
class CanonicalTargetSpecificationKey final {
public:
  CanonicalTargetSpecificationKey(CanonicalTargetSpecificationKey&&) noexcept = default;
  CanonicalTargetSpecificationKey& operator=(CanonicalTargetSpecificationKey&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalTargetSpecificationKey);

  ZC_NODISCARD static zc::Maybe<CanonicalTargetSpecificationKey> from(
      TargetComponentName&& architecture, TargetComponentName&& vendor,
      TargetComponentName&& operatingSystem, TargetComponentName&& environment,
      TargetComponentName&& abi, uint32_t pointerWidth, Endianness endianness,
      SortedTargetFeatureSet&& semanticFeatures);
  ZC_NODISCARD static zc::Maybe<CanonicalTargetSpecificationKey> decodeCanonical(
      CanonicalDecoder& decoder);
  ZC_NODISCARD CanonicalTargetSpecificationKey clone() const;
  /// \brief Returns the canonical target architecture component.
  ZC_NODISCARD zc::StringPtr architecture() const noexcept;
  /// \brief Returns the canonical target pointer width in bits.
  ZC_NODISCARD uint32_t pointerWidth() const noexcept;
  /// \brief Returns the canonical target byte order.
  ZC_NODISCARD Endianness endianness() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  CanonicalTargetSpecificationKey(TargetComponentName&& architecture, TargetComponentName&& vendor,
                                  TargetComponentName&& operatingSystem,
                                  TargetComponentName&& environment, TargetComponentName&& abi,
                                  uint32_t pointerWidth, Endianness endianness,
                                  SortedTargetFeatureSet&& semanticFeatures) noexcept;

  TargetComponentName architectureValue;
  TargetComponentName vendorValue;
  TargetComponentName operatingSystemValue;
  TargetComponentName environmentValue;
  TargetComponentName abiValue;
  uint32_t pointerWidthValue;
  Endianness endiannessValue;
  SortedTargetFeatureSet featureValue;
};

/// \brief Closed source-semantic compiler option key.
class SemanticCompilerOptionsKey final {
public:
  ZC_NODISCARD static SemanticCompilerOptionsKey from(uint32_t editionYear, bool useUnicode,
                                                      bool allowDollarIdentifiers,
                                                      bool supportRegexLiterals) noexcept;
  ZC_NODISCARD static zc::Maybe<SemanticCompilerOptionsKey> decodeCanonical(
      CanonicalDecoder& decoder);
  void encode(CanonicalEncoder& encoder) const;

private:
  SemanticCompilerOptionsKey(uint32_t editionYear, bool useUnicode, bool allowDollarIdentifiers,
                             bool supportRegexLiterals) noexcept;

  uint32_t editionYearValue;
  bool useUnicodeValue;
  bool allowDollarIdentifiersValue;
  bool supportRegexLiteralsValue;
};

/// \brief Stable identity of one configured build-script producer plan.
class BuildScriptProducerKey final {
public:
  ZC_NODISCARD static BuildScriptProducerKey from(const Sha256Digest& digest) noexcept;
  ZC_NODISCARD static zc::Maybe<BuildScriptProducerKey> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD const Sha256Digest& digest() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit BuildScriptProducerKey(const Sha256Digest& digest) noexcept;

  Sha256Digest value;
};

enum class CompilationDomain : uint8_t { Host = 0x01, Target = 0x02 };

/// \brief Complete host-or-target semantic compilation configuration.
class CompilationConfigKey final {
public:
  CompilationConfigKey(CompilationConfigKey&&) noexcept = default;
  CompilationConfigKey& operator=(CompilationConfigKey&&) noexcept = default;
  ZC_DISALLOW_COPY(CompilationConfigKey);

  ZC_NODISCARD static zc::Maybe<CompilationConfigKey> from(
      CompilationDomain domain, CanonicalTargetSpecificationKey&& target,
      SemanticCompilerOptionsKey semanticOptions,
      zc::Maybe<BuildScriptProducerKey>&& buildScriptProducer);
  ZC_NODISCARD static zc::Maybe<CompilationConfigKey> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD CompilationConfigKey clone() const;
  ZC_NODISCARD const SemanticCompilerOptionsKey& semanticOptions() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  CompilationConfigKey(CompilationDomain domain, CanonicalTargetSpecificationKey&& target,
                       SemanticCompilerOptionsKey semanticOptions,
                       zc::Maybe<BuildScriptProducerKey>&& buildScriptProducer) noexcept;

  CompilationDomain domainValue;
  CanonicalTargetSpecificationKey targetValue;
  SemanticCompilerOptionsKey semanticOptionsValue;
  zc::Maybe<BuildScriptProducerKey> buildScriptProducerValue;
};

enum class CrateTargetKind : uint8_t {
  Library = 0x01,
  Binary = 0x02,
  Test = 0x03,
  Benchmark = 0x04,
  Example = 0x05,
  BuildScript = 0x06
};

/// \brief Complete canonical crate compilation-root key.
class CrateKey final {
public:
  CrateKey(CrateKey&&) noexcept = default;
  CrateKey& operator=(CrateKey&&) noexcept = default;
  ZC_DISALLOW_COPY(CrateKey);

  ZC_NODISCARD static zc::Maybe<CrateKey> from(PackageKey&& package, CrateTargetKind kind,
                                               TargetName&& targetName,
                                               CompilationConfigKey&& compilation);
  ZC_NODISCARD static zc::Maybe<CrateKey> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD CrateKey clone() const;
  ZC_NODISCARD const PackageKey& package() const noexcept;
  ZC_NODISCARD CrateTargetKind targetKind() const noexcept;
  ZC_NODISCARD zc::StringPtr targetName() const noexcept;
  ZC_NODISCARD const SemanticCompilerOptionsKey& semanticOptions() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  CrateKey(PackageKey&& package, CrateTargetKind kind, TargetName&& targetName,
           CompilationConfigKey&& compilation) noexcept;

  PackageKey packageValue;
  CrateTargetKind kindValue;
  TargetName targetNameValue;
  CompilationConfigKey compilationValue;
};

/// \brief Exact crate-to-crate expansion of one resolved package edge.
class CrateDependencyEdgeKey final {
public:
  CrateDependencyEdgeKey(CrateDependencyEdgeKey&&) noexcept = default;
  CrateDependencyEdgeKey& operator=(CrateDependencyEdgeKey&&) noexcept = default;
  ZC_DISALLOW_COPY(CrateDependencyEdgeKey);

  ZC_NODISCARD static zc::Maybe<CrateDependencyEdgeKey> from(PackageDependencyEdgeKey&& packageEdge,
                                                             CrateKey&& consumer,
                                                             CrateKey&& provider);
  ZC_NODISCARD static zc::Maybe<CrateDependencyEdgeKey> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD CrateDependencyEdgeKey clone() const;
  ZC_NODISCARD const PackageDependencyEdgeKey& packageEdge() const noexcept;
  ZC_NODISCARD const CrateKey& consumer() const noexcept;
  ZC_NODISCARD const CrateKey& provider() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  CrateDependencyEdgeKey(PackageDependencyEdgeKey&& packageEdge, CrateKey&& consumer,
                         CrateKey&& provider) noexcept;

  PackageDependencyEdgeKey packageEdgeValue;
  CrateKey consumerValue;
  CrateKey providerValue;
};

}  // namespace zomlang::compiler::identity
