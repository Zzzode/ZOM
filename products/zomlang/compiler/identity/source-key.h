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
#include "zomlang/compiler/identity/crate-key.h"

namespace zomlang::compiler::identity {

class CanonicalEncoder;
class ImmutableSourceSnapshot;

struct LocalFileSourceOrigin final {
  CanonicalWorkspaceRelativePath canonicalPath;
};

struct RegistryFileSourceOrigin final {
  PackageKey package;
  CanonicalRelativePath path;
};

struct VcsFileSourceOrigin final {
  PackageKey package;
  CanonicalRelativePath path;
};

struct GeneratedFileSourceOrigin final {
  BuildScriptOutputKey buildScriptOutput;
  CanonicalRelativePath logicalPath;
  Sha256Digest contentDigest;
};

enum class SourceOriginKind : uint8_t {
  LocalFile = 0x01,
  RegistryFile = 0x02,
  VcsFile = 0x03,
  GeneratedFile = 0x04
};

/// \brief Closed canonical source provenance union.
class SourceOriginKey final {
public:
  SourceOriginKey(SourceOriginKey&&) noexcept = default;
  SourceOriginKey& operator=(SourceOriginKey&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceOriginKey);

  ZC_NODISCARD static SourceOriginKey localFile(CanonicalWorkspaceRelativePath&& path);
  ZC_NODISCARD static SourceOriginKey registryFile(PackageKey&& package,
                                                   CanonicalRelativePath&& path);
  ZC_NODISCARD static SourceOriginKey vcsFile(PackageKey&& package, CanonicalRelativePath&& path);
  ZC_NODISCARD static SourceOriginKey generatedFile(BuildScriptOutputKey buildScriptOutput,
                                                    CanonicalRelativePath&& logicalPath,
                                                    const Sha256Digest& contentDigest);
  ZC_NODISCARD SourceOriginKey clone() const;
  ZC_NODISCARD SourceOriginKind kind() const noexcept;
  ZC_NODISCARD bool acceptsContentDigest(const Sha256Digest& contentDigest) const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  explicit SourceOriginKey(LocalFileSourceOrigin&& value) noexcept;
  explicit SourceOriginKey(RegistryFileSourceOrigin&& value) noexcept;
  explicit SourceOriginKey(VcsFileSourceOrigin&& value) noexcept;
  explicit SourceOriginKey(GeneratedFileSourceOrigin&& value) noexcept;

  zc::OneOf<LocalFileSourceOrigin, RegistryFileSourceOrigin, VcsFileSourceOrigin,
            GeneratedFileSourceOrigin>
      value;
};

/// \brief Canonical source file identity under one expanded crate key.
class SourceFileKey final {
public:
  SourceFileKey(SourceFileKey&&) noexcept = default;
  SourceFileKey& operator=(SourceFileKey&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceFileKey);

  ZC_NODISCARD static SourceFileKey from(CrateKey&& crate, SourceOriginKey&& origin);
  ZC_NODISCARD SourceFileKey clone() const;
  ZC_NODISCARD const CrateKey& crate() const noexcept;
  ZC_NODISCARD bool sameAs(const SourceFileKey& other) const;
  ZC_NODISCARD bool belongsTo(const CrateKey& crate) const;
  ZC_NODISCARD bool acceptsContentDigest(const Sha256Digest& contentDigest) const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  SourceFileKey(CrateKey&& crate, SourceOriginKey&& origin) noexcept;

  CrateKey crateValue;
  SourceOriginKey originValue;
};

/// \brief Source-file-qualified half-open canonical byte range.
class SourceSpan final {
public:
  SourceSpan(SourceSpan&&) noexcept = default;
  SourceSpan& operator=(SourceSpan&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceSpan);

  ZC_NODISCARD SourceSpan clone() const;
  ZC_NODISCARD bool belongsTo(const SourceFileKey& source) const;
  ZC_NODISCARD uint64_t byteStart() const noexcept;
  ZC_NODISCARD uint64_t byteEnd() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  SourceSpan(SourceFileKey&& source, uint64_t byteStart, uint64_t byteEnd) noexcept;

  SourceFileKey sourceValue;
  uint64_t startValue;
  uint64_t endValue;

  friend class ImmutableSourceSnapshot;
};

/// \brief Frozen canonical module identity expanded through crate and source keys.
class ModuleKey final {
public:
  ModuleKey(ModuleKey&&) noexcept = default;
  ModuleKey& operator=(ModuleKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleKey);

  ZC_NODISCARD static zc::Maybe<ModuleKey> from(CrateKey&& crate,
                                                zc::Vector<ModulePathSegment>&& canonicalPath,
                                                SourceFileKey&& source,
                                                zc::Maybe<SourceSpan>&& declarationAnchor);
  ZC_NODISCARD ModuleKey clone() const;
  ZC_NODISCARD const CrateKey& crate() const noexcept;
  ZC_NODISCARD const SourceFileKey& source() const noexcept;
  ZC_NODISCARD bool contains(const SourceSpan& span) const;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ModuleKey(CrateKey&& crate, zc::Vector<ModulePathSegment>&& canonicalPath, SourceFileKey&& source,
            zc::Maybe<SourceSpan>&& declarationAnchor) noexcept;

  CrateKey crateValue;
  zc::Vector<ModulePathSegment> pathValue;
  SourceFileKey sourceValue;
  zc::Maybe<SourceSpan> declarationAnchorValue;
};

}  // namespace zomlang::compiler::identity
