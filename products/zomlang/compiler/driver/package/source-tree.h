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
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/package/archive-reader.h"
#include "zomlang/compiler/identity/package-key.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::identity {
class CanonicalEncoder;
}

namespace zomlang::compiler::driver::package {

/// \brief Canonical inventory record for one admitted regular source file.
class SourceTreeFile final {
public:
  ZC_NODISCARD static SourceTreeFile from(identity::CanonicalRelativePath&& path,
                                          uint64_t byteLength,
                                          const identity::Sha256Digest& contentDigest);

  SourceTreeFile(SourceTreeFile&&) noexcept = default;
  SourceTreeFile& operator=(SourceTreeFile&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceTreeFile);

  ZC_NODISCARD SourceTreeFile clone() const;
  ZC_NODISCARD const identity::CanonicalRelativePath& path() const noexcept;
  ZC_NODISCARD uint64_t byteLength() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  SourceTreeFile(identity::CanonicalRelativePath&& path, uint64_t byteLength,
                 const identity::Sha256Digest& contentDigest) noexcept;

  identity::CanonicalRelativePath pathValue;
  uint64_t byteLengthValue;
  identity::Sha256Digest contentDigestValue;
};

/// \brief Sorted immutable source inventory and its domain-separated tree digest.
class SourceTreeRecord final {
public:
  ZC_NODISCARD static zc::Maybe<SourceTreeRecord> from(zc::Vector<SourceTreeFile>&& files);

  SourceTreeRecord(SourceTreeRecord&&) noexcept = default;
  SourceTreeRecord& operator=(SourceTreeRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceTreeRecord);

  ZC_NODISCARD SourceTreeRecord clone() const;
  ZC_NODISCARD zc::ArrayPtr<const SourceTreeFile> files() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

private:
  SourceTreeRecord(zc::Vector<SourceTreeFile>&& files,
                   const identity::Sha256Digest& digest) noexcept;

  zc::Vector<SourceTreeFile> fileValues;
  identity::Sha256Digest digestValue;
};

using SourceTreeBuildResult = zc::OneOf<SourceTreeRecord, MaterializationIssue>;

/// \brief Streaming archive output that validates paths and computes a source-tree record.
class SourceTreeBuilder final : public ArchiveOutput {
public:
  SourceTreeBuilder();
  ~SourceTreeBuilder() noexcept(false) override;
  SourceTreeBuilder(SourceTreeBuilder&&) noexcept;
  SourceTreeBuilder& operator=(SourceTreeBuilder&&) noexcept;
  ZC_DISALLOW_COPY(SourceTreeBuilder);

  zc::Maybe<MaterializationIssue> beginFile(zc::StringPtr path, uint64_t byteLength) override;
  zc::Maybe<MaterializationIssue> write(zc::ArrayPtr<const zc::byte> bytes) override;
  zc::Maybe<MaterializationIssue> endFile() override;

  /// \brief Finalizes the immutable record after the archive reader reaches EOF.
  ZC_NODISCARD SourceTreeBuildResult finish();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::package
