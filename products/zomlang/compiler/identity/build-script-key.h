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
#include "zomlang/compiler/identity/crate-key.h"

namespace zomlang::compiler::identity {

class CanonicalEncoder;

/// \brief One canonical path-to-content-digest map entry.
class BuildScriptDigestEntry final {
public:
  ZC_NODISCARD static BuildScriptDigestEntry from(CanonicalRelativePath&& path,
                                                  const Sha256Digest& digest);
  BuildScriptDigestEntry(BuildScriptDigestEntry&&) noexcept = default;
  BuildScriptDigestEntry& operator=(BuildScriptDigestEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptDigestEntry);

  ZC_NODISCARD BuildScriptDigestEntry clone() const;
  ZC_NODISCARD const CanonicalRelativePath& path() const noexcept;
  ZC_NODISCARD const Sha256Digest& digest() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  BuildScriptDigestEntry(CanonicalRelativePath&& path, const Sha256Digest& digest) noexcept;
  CanonicalRelativePath pathValue;
  Sha256Digest digestValue;
};

/// \brief One canonical semantic-environment map entry with arbitrary bounded bytes.
class BuildScriptEnvironmentEntry final {
public:
  ZC_NODISCARD static BuildScriptEnvironmentEntry from(SemanticEnvironmentName&& name,
                                                       zc::Array<uint8_t>&& value);
  BuildScriptEnvironmentEntry(BuildScriptEnvironmentEntry&&) noexcept = default;
  BuildScriptEnvironmentEntry& operator=(BuildScriptEnvironmentEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptEnvironmentEntry);

  ZC_NODISCARD BuildScriptEnvironmentEntry clone() const;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> value() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  BuildScriptEnvironmentEntry(SemanticEnvironmentName&& name, zc::Array<uint8_t>&& value) noexcept;
  SemanticEnvironmentName nameValue;
  zc::Array<uint8_t> byteValue;
};

/// \brief Output-independent RFC 0011 identity for one selected build script.
class PreparatoryBuildScriptKey final {
public:
  ZC_NODISCARD static zc::Maybe<PreparatoryBuildScriptKey> from(
      PackageKey&& package, TargetName&& targetName, CanonicalTargetSpecificationKey&& hostTarget,
      SemanticCompilerOptionsKey semanticOptions, zc::Vector<PackageKey>&& buildDependencies);
  PreparatoryBuildScriptKey(PreparatoryBuildScriptKey&&) noexcept = default;
  PreparatoryBuildScriptKey& operator=(PreparatoryBuildScriptKey&&) noexcept = default;
  ZC_DISALLOW_COPY(PreparatoryBuildScriptKey);

  ZC_NODISCARD PreparatoryBuildScriptKey clone() const;
  ZC_NODISCARD const PackageKey& package() const noexcept;
  ZC_NODISCARD zc::StringPtr targetName() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const PackageKey> buildDependencies() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  PreparatoryBuildScriptKey(PackageKey&& package, TargetName&& targetName,
                            CanonicalTargetSpecificationKey&& hostTarget,
                            SemanticCompilerOptionsKey semanticOptions,
                            zc::Vector<PackageKey>&& buildDependencies) noexcept;
  PackageKey packageValue;
  TargetName targetNameValue;
  CanonicalTargetSpecificationKey hostTargetValue;
  SemanticCompilerOptionsKey semanticOptionsValue;
  zc::Vector<PackageKey> buildDependencyValues;
};

/// \brief Complete deterministic build-script output identity record.
class BuildScriptOutputRecord final {
public:
  ZC_NODISCARD static zc::Maybe<BuildScriptOutputRecord> from(
      PreparatoryBuildScriptKey&& preparatory, zc::Vector<BuildScriptDigestEntry>&& sourceDigests,
      zc::Vector<BuildScriptEnvironmentEntry>&& declaredEnvironment,
      zc::Vector<BuildScriptDigestEntry>&& generatedSources,
      zc::Vector<BuildScriptEnvironmentEntry>&& exportedSemanticEnvironment);
  BuildScriptOutputRecord(BuildScriptOutputRecord&&) noexcept = default;
  BuildScriptOutputRecord& operator=(BuildScriptOutputRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptOutputRecord);

  ZC_NODISCARD BuildScriptOutputRecord clone() const;
  ZC_NODISCARD const PreparatoryBuildScriptKey& preparatoryKey() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BuildScriptDigestEntry> sourceDigests() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BuildScriptEnvironmentEntry> declaredEnvironment() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BuildScriptDigestEntry> generatedSources() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BuildScriptEnvironmentEntry> exportedEnvironment() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  ZC_NODISCARD BuildScriptOutputKey outputKey() const;

private:
  BuildScriptOutputRecord(
      PreparatoryBuildScriptKey&& preparatory, zc::Vector<BuildScriptDigestEntry>&& sourceDigests,
      zc::Vector<BuildScriptEnvironmentEntry>&& declaredEnvironment,
      zc::Vector<BuildScriptDigestEntry>&& generatedSources,
      zc::Vector<BuildScriptEnvironmentEntry>&& exportedSemanticEnvironment) noexcept;
  PreparatoryBuildScriptKey preparatoryValue;
  zc::Vector<BuildScriptDigestEntry> sourceDigestValues;
  zc::Vector<BuildScriptEnvironmentEntry> declaredEnvironmentValues;
  zc::Vector<BuildScriptDigestEntry> generatedSourceValues;
  zc::Vector<BuildScriptEnvironmentEntry> exportedEnvironmentValues;
};

}  // namespace zomlang::compiler::identity
