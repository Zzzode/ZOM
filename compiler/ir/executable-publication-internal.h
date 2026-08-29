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

#include "compiler/ir/executable-publication.h"

namespace zomlang::compiler::ir {

enum class PublicationCheckpoint : uint8_t {
  StartedDurable = 0x01,
  ManifestTemporaryDurable = 0x02,
  ManifestStagedDurable = 0x03,
  ExecutableRenamed = 0x04,
  ExecutableDirectoryDurable = 0x05,
  ExecCommittedDurable = 0x06,
  ManifestRenamed = 0x07,
  ManifestDirectoryDurable = 0x08,
  ManifestCommittedDurable = 0x09,
};

class PublicationCheckpointObserver {
public:
  virtual ~PublicationCheckpointObserver() noexcept = default;
  virtual void reached(PublicationCheckpoint checkpoint) = 0;
};

struct PublicationTransactionTestAccess final {
  ZC_NODISCARD static PublicationOutcome publishObserved(
      LinkedOutputCandidate candidate, VerifiedExecutableManifest manifest,
      PublicationCheckpointObserver& observer);
};

}  // namespace zomlang::compiler::ir
