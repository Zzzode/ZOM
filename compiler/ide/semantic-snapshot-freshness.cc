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

#include "compiler/ide/semantic-snapshot-freshness.h"

#include "compiler/ide/semantic-snapshot-facade.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/identity/source-query-input.h"
#include "compiler/parser/query/parse-source-query.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ide {
namespace {

// Reads one input's presence and last-change revision on `snapshot`. An absent
// input reports present=false with a default revision; a present input reports
// its metadata `changedAt`, the same coordinate the incremental engine compares
// against `verifiedAt` when it revalidates a memo.
template <typename Input>
SemanticSnapshotInputStamp stampInput(const query::QuerySnapshot& snapshot,
                                      const typename Input::Key& key) {
  auto metadata = snapshot.metadata<Input>(key);
  ZC_IF_SOME(present, metadata) { return SemanticSnapshotInputStamp{true, present.changedAt()}; }
  return SemanticSnapshotInputStamp{false, query::DatabaseRevision()};
}

// Confirms every input the parse recorded reading on this snapshot is one of the
// two the facade tracks: the source snapshot and the compilation options. A
// dependency on any other input means ParseSourceQuery reads an input this stamp
// does not capture, so the single-hop approximation would silently under-report
// and must fail closed instead. An input may be recorded more than once (the
// provider and its verifier each read it); the invariant is that both tracked
// inputs appear at least once and nothing else appears at all.
bool dependenciesAreExactlyTrackedInputs(
    const query::QuerySnapshot& snapshot,
    const identity::source_query::StableSourceQueryKey& sourceKey,
    const identity::CrateKey& crate) {
  auto sourceFingerprint =
      snapshot.keyFingerprint<identity::source_query::SourceSnapshotInput>(sourceKey);
  auto optionsFingerprint =
      snapshot.keyFingerprint<identity::source_query::CompilationOptionsInput>(crate);
  if (sourceFingerprint == zc::none || optionsFingerprint == zc::none) { return false; }

  auto groups = snapshot.dependencies<parser::ParseSourceQuery>(sourceKey);
  size_t sourceReads = 0;
  size_t optionsReads = 0;
  for (const auto& group : groups) {
    for (const auto& dependency : group.dependencies()) {
      const auto& fingerprint = dependency.key().fingerprint();
      if (fingerprint == ZC_ASSERT_NONNULL(sourceFingerprint)) {
        ++sourceReads;
      } else if (fingerprint == ZC_ASSERT_NONNULL(optionsFingerprint)) {
        ++optionsReads;
      } else {
        return false;
      }
    }
  }
  return sourceReads >= 1 && optionsReads >= 1;
}

}  // namespace

zc::Maybe<SemanticSnapshotFreshness> SemanticSnapshotFreshness::seal(
    const query::QuerySnapshot& snapshot, const SemanticSnapshotKey& key) {
  identity::CanonicalDecoder decoder(key.sourceKey().canonicalSourceBytes());
  auto sourceFile = identity::SourceFileKey::decodeCanonical(decoder);
  if (sourceFile == zc::none || !decoder.finished()) { return zc::none; }
  auto crate = ZC_ASSERT_NONNULL(sourceFile).crate().clone();

  if (!dependenciesAreExactlyTrackedInputs(snapshot, key.sourceKey(), crate)) { return zc::none; }

  auto sourceStamp =
      stampInput<identity::source_query::SourceSnapshotInput>(snapshot, key.sourceKey());
  auto optionsStamp = stampInput<identity::source_query::CompilationOptionsInput>(snapshot, crate);
  // A published or source-rejected parse necessarily read both inputs, so both
  // must be present; an absent input here is an inconsistency, fail closed.
  if (!sourceStamp.present || !optionsStamp.present) { return zc::none; }

  return SemanticSnapshotFreshness(snapshot.revision(), key.sourceKey().clone(), zc::mv(crate),
                                   sourceStamp, optionsStamp);
}

bool SemanticSnapshotFreshness::isFreshAgainst(const query::QuerySnapshot& current) const {
  auto sourceStamp =
      stampInput<identity::source_query::SourceSnapshotInput>(current, sourceKeyValue);
  auto optionsStamp =
      stampInput<identity::source_query::CompilationOptionsInput>(current, crateValue);
  return sourceStamp == sourceStampValue && optionsStamp == optionsStampValue;
}

TrackedSemanticSnapshot resolveSemanticSnapshotTracked(query::QueryDatabase& database,
                                                       const SemanticSnapshotKey& key) {
  // Resolve and seal on the SAME snapshot: the parse capability memo is
  // revision-local, so its recorded dependencies (which the seal cross-checks)
  // are only observable on the snapshot the demand ran on.
  auto snapshot = database.snapshot();
  auto resolved = resolveSemanticSnapshot(snapshot, key);

  // Only a published or source-rejected result reflects a parse that read the
  // tracked inputs; an unavailable result has nothing to stamp.
  if (resolved.isUnavailable()) { return TrackedSemanticSnapshot{zc::mv(resolved), zc::none}; }

  auto freshness = SemanticSnapshotFreshness::seal(snapshot, key);
  if (freshness == zc::none) {
    // The stamp could not be cross-checked on this snapshot; degrade rather than
    // hand back a result that cannot be validated for staleness.
    return TrackedSemanticSnapshot{
        SemanticSnapshot::unavailable(SnapshotUnavailableReason::EvaluationRejected), zc::none};
  }
  return TrackedSemanticSnapshot{zc::mv(resolved), zc::mv(freshness)};
}

}  // namespace zomlang::compiler::ide
