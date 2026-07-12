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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/driver/package/materialization-issue.h"
#include "zomlang/compiler/driver/package/source-admission-limits.h"

namespace zomlang::compiler::driver::package {

class ArchiveInput;

struct ZstdInputData final {
  size_t byteCount;
};

struct ZstdInputEnd final {};

using ZstdInputResult = zc::OneOf<ZstdInputData, ZstdInputEnd, MaterializationIssue>;
using ZstdDecodedInputResult = zc::OneOf<zc::Own<ArchiveInput>, MaterializationIssue>;

/// \brief Pull-based bounded compressed-byte source.
class ZstdInput {
public:
  virtual ~ZstdInput() noexcept(false) = default;

  /// \brief Fill at most `destination.size()` bytes.
  virtual ZstdInputResult read(zc::ArrayPtr<zc::byte> destination) = 0;
};

/// \brief Push-based decompressed-byte consumer.
class ZstdOutput {
public:
  virtual ~ZstdOutput() noexcept(false) = default;

  /// \brief Consume one bounded output chunk, returning none on success.
  virtual zc::Maybe<MaterializationIssue> write(zc::ArrayPtr<const zc::byte> bytes) = 0;
};

/// \brief Bounded single-frame Zstandard streaming decoder.
class ZstdDecoder final {
public:
  explicit ZstdDecoder(SourceAdmissionLimits limits = {});
  ~ZstdDecoder() noexcept(false);

  ZstdDecoder(ZstdDecoder&&) noexcept;
  ZstdDecoder& operator=(ZstdDecoder&&) noexcept;
  ZC_DISALLOW_COPY(ZstdDecoder);

  /// \brief Decode exactly one frame and reject any trailing compressed bytes.
  ZC_NODISCARD zc::Maybe<MaterializationIssue> decode(ZstdInput& input, ZstdOutput& output);

  /// \brief Opens one pull-based decoded stream for direct archive admission.
  ZC_NODISCARD ZstdDecodedInputResult openDecodedInput(ZstdInput& input) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::package
