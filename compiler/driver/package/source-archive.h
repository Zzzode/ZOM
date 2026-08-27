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

#include "zc/core/common.h"
#include "compiler/driver/package/source-tree.h"
#include "compiler/driver/package/zstd-decoder.h"

namespace zomlang::compiler::driver::package {

/// \brief Streaming Zstandard-to-ustar admission pipeline.
class SourceArchiveAdmission final {
public:
  explicit SourceArchiveAdmission(SourceAdmissionLimits limits = {});

  SourceArchiveAdmission(SourceArchiveAdmission&&) noexcept = default;
  SourceArchiveAdmission& operator=(SourceArchiveAdmission&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceArchiveAdmission);

  ZC_NODISCARD SourceTreeBuildResult admit(ZstdInput& input);

private:
  SourceAdmissionLimits limits;
};

}  // namespace zomlang::compiler::driver::package
