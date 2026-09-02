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

#include "compiler/ide/semantic-snapshot.h"

namespace zomlang::compiler::ide {

SemanticSnapshot SemanticSnapshot::published(zc::ArrayPtr<const uint8_t> sourceKeyBytes,
                                             uint64_t sourceByteLength, DocumentVersion version,
                                             zc::Array<SnapshotToken>&& tokens,
                                             zc::Array<SnapshotDiagnostic>&& diagnostics) {
  return SemanticSnapshot(Kind::Published, zc::heapArray<uint8_t>(sourceKeyBytes), sourceByteLength,
                          version, zc::mv(tokens), zc::mv(diagnostics),
                          SnapshotUnavailableReason::EvaluationRejected);
}

SemanticSnapshot SemanticSnapshot::sourceRejected(DocumentVersion version,
                                                  zc::Array<SnapshotDiagnostic>&& diagnostics) {
  return SemanticSnapshot(Kind::SourceRejected, zc::heapArray<uint8_t>(0), 0, version,
                          zc::Array<SnapshotToken>(), zc::mv(diagnostics),
                          SnapshotUnavailableReason::EvaluationRejected);
}

SemanticSnapshot SemanticSnapshot::unavailable(SnapshotUnavailableReason reason) {
  return SemanticSnapshot(Kind::Unavailable, zc::heapArray<uint8_t>(0), 0,
                          DocumentVersion::initial(0), zc::Array<SnapshotToken>(),
                          zc::Array<SnapshotDiagnostic>(), reason);
}

}  // namespace zomlang::compiler::ide
