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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0023 "IDE Semantic Snapshots": prove the SemanticSnapshot result is a
// closed three-arm projection (Published / SourceRejected / Unavailable) that
// carries only IDE-safe values and no compiler handle. This composes as pure
// data; it drives no query.

#include "compiler/ide/semantic-snapshot.h"

#include <cstdint>

#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "compiler/ide/document-version.h"
#include "compiler/ide/snapshot-diagnostic.h"
#include "zc/core/array.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ide {
namespace {

zc::Array<SnapshotDiagnostic> oneWarning() {
  zc::Vector<SnapshotDiagnostic> facts(1);
  facts.add(SnapshotDiagnostic::projectRanged(diagnostics::DiagID::CheckerUnreachableMatchArm,
                                              SnapshotRange{4, 9, true},
                                              zc::heapArray<zc::String>(0).asPtr()));
  return facts.releaseAsArray();
}

zc::Array<SnapshotDiagnostic> oneRangelessError() {
  zc::Vector<SnapshotDiagnostic> facts(1);
  facts.add(SnapshotDiagnostic::projectRangeless(diagnostics::DiagID::InvalidCharacter,
                                                 zc::heapArray<zc::String>(0).asPtr()));
  return facts.releaseAsArray();
}

ZC_TEST("SemanticSnapshot published arm exposes source identity and warnings") {
  const uint8_t keyBytes[] = {0x01, 0x02, 0x03};
  auto snapshot = SemanticSnapshot::published(zc::arrayPtr(keyBytes), 42,
                                              DocumentVersion::initial(7), oneWarning());
  ZC_EXPECT(snapshot.kind() == SemanticSnapshot::Kind::Published);
  ZC_EXPECT(snapshot.isPublished());
  ZC_EXPECT(!snapshot.isSourceRejected());
  ZC_EXPECT(!snapshot.isUnavailable());
  ZC_EXPECT(snapshot.sourceByteLength() == 42);
  ZC_EXPECT(snapshot.documentVersion() == DocumentVersion::initial(7));
  ZC_EXPECT(snapshot.sourceKeyBytes() == zc::arrayPtr(keyBytes));
  ZC_EXPECT(snapshot.diagnostics().size() == 1);
  ZC_EXPECT(snapshot.diagnostics()[0].severity() == diagnostics::DiagSeverity::kWarning);
}

ZC_TEST("SemanticSnapshot published arm owns a copy of the source key bytes") {
  auto keyBytes = zc::heapArray<uint8_t>(3);
  keyBytes[0] = 0x0a;
  keyBytes[1] = 0x0b;
  keyBytes[2] = 0x0c;
  auto snapshot = SemanticSnapshot::published(keyBytes.asPtr(), 3, DocumentVersion::initial(1),
                                              zc::Array<SnapshotDiagnostic>());
  keyBytes[0] = 0xff;  // mutate the source after projection
  ZC_EXPECT(snapshot.sourceKeyBytes()[0] == 0x0a);
}

ZC_TEST("SemanticSnapshot source-rejected arm carries error diagnostics without a range") {
  auto snapshot =
      SemanticSnapshot::sourceRejected(DocumentVersion::initial(-3), oneRangelessError());
  ZC_EXPECT(snapshot.kind() == SemanticSnapshot::Kind::SourceRejected);
  ZC_EXPECT(snapshot.isSourceRejected());
  ZC_EXPECT(snapshot.documentVersion() == DocumentVersion::initial(-3));
  ZC_EXPECT(snapshot.diagnostics().size() == 1);
  ZC_EXPECT(snapshot.diagnostics()[0].severity() == diagnostics::DiagSeverity::kError);
  ZC_EXPECT(snapshot.diagnostics()[0].range() == zc::none);
}

ZC_TEST("SemanticSnapshot unavailable arm carries a closed reason and no diagnostics") {
  auto snapshot = SemanticSnapshot::unavailable(SnapshotUnavailableReason::Cancelled);
  ZC_EXPECT(snapshot.kind() == SemanticSnapshot::Kind::Unavailable);
  ZC_EXPECT(snapshot.isUnavailable());
  ZC_EXPECT(snapshot.unavailableReason() == SnapshotUnavailableReason::Cancelled);
  ZC_EXPECT(snapshot.diagnostics().size() == 0);
}

ZC_TEST("SemanticSnapshot unavailable arm distinguishes each reason") {
  ZC_EXPECT(
      SemanticSnapshot::unavailable(SnapshotUnavailableReason::MissingInput).unavailableReason() ==
      SnapshotUnavailableReason::MissingInput);
  ZC_EXPECT(SemanticSnapshot::unavailable(SnapshotUnavailableReason::EvaluationRejected)
                .unavailableReason() == SnapshotUnavailableReason::EvaluationRejected);
}

}  // namespace
}  // namespace zomlang::compiler::ide
