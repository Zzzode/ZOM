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

namespace zomlang::compiler::driver::package {

/// \brief Closed default limits for RFC 0012 source admission.
struct SourceAdmissionLimits final {
  uint64_t compressedArchiveBytes = 536870912;
  uint64_t zstdWindowBytes = 67108864;
  uint64_t decoderWorkingBytes = 134217728;
  uint64_t archiveHeaderCount = 100000;
  uint64_t archiveMetadataBytes = 67108864;
  uint64_t fileCount = 100000;
  uint64_t singleFileBytes = 67108864;
  uint64_t totalFileBytes = 2147483648;
  uint64_t ioChunkBytes = 1048576;
};

}  // namespace zomlang::compiler::driver::package
