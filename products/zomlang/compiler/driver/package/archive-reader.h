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

#include <cstddef>
#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zomlang/compiler/driver/package/materialization-issue.h"
#include "zomlang/compiler/driver/package/source-admission-limits.h"

namespace zomlang::compiler::driver::package {

struct ArchiveInputData final {
  size_t byteCount;
};

struct ArchiveInputEnd final {};

using ArchiveInputResult = zc::OneOf<ArchiveInputData, ArchiveInputEnd, MaterializationIssue>;

/// \brief Pull-based bounded uncompressed archive source.
class ArchiveInput {
public:
  virtual ~ArchiveInput() noexcept(false) = default;

  /// \brief Fill at most `destination.size()` bytes.
  virtual ArchiveInputResult read(zc::ArrayPtr<zc::byte> destination) = 0;
};

/// \brief Push-based regular-file consumer for an admitted archive.
class ArchiveOutput {
public:
  virtual ~ArchiveOutput() noexcept(false) = default;

  /// \brief Begin one regular file with its exact raw UTF-8 path and size.
  virtual zc::Maybe<MaterializationIssue> beginFile(zc::StringPtr path, uint64_t byteLength) = 0;

  /// \brief Consume one bounded file-data chunk.
  virtual zc::Maybe<MaterializationIssue> write(zc::ArrayPtr<const zc::byte> bytes) = 0;

  /// \brief Finish the current regular file.
  virtual zc::Maybe<MaterializationIssue> endFile() = 0;
};

/// \brief Bounded streaming reader for exactly one POSIX ustar archive.
class ArchiveReader final {
public:
  explicit ArchiveReader(SourceAdmissionLimits limits = {});
  ~ArchiveReader() noexcept(false);

  ArchiveReader(ArchiveReader&&) noexcept;
  ArchiveReader& operator=(ArchiveReader&&) noexcept;
  ZC_DISALLOW_COPY(ArchiveReader);

  /// \brief Read one ustar archive and reject every non-regular entry kind.
  ZC_NODISCARD zc::Maybe<MaterializationIssue> read(ArchiveInput& input, ArchiveOutput& output);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::package
