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

#include "compiler/ide/semantic-snapshot-key.h"
#include "compiler/ide/semantic-snapshot.h"
#include "compiler/query/query-database.h"
#include "compiler/query/query-types.h"

namespace zomlang::compiler::ide {

/// \brief The recorded change coordinate of one parse-query input at seal time.
///
/// Captures whether the input was present and the revision at which its value
/// last changed. This is the raw material a freshness comparison uses; it is not
/// itself a decision.
struct SemanticSnapshotInputStamp final {
  bool present = false;
  query::DatabaseRevision changedAt;

  bool operator==(const SemanticSnapshotInputStamp& other) const noexcept = default;
};

/// \brief A single-hop, non-transitive freshness stamp for one resolved snapshot.
///
/// This is a deliberately narrow, honest approximation of change tracking. It is
/// NOT the RFC 0023 "Analysis lease": it does not collect a transitive input
/// frontier, it does not run under a sealed atomic validate-and-publish window,
/// and it must never be named a lease or claim `InputsCurrent`/`InputsChanged`
/// semantics. It records the source-determining inputs -- the workspace source
/// snapshot, the editor overlay, the source selection, and the compilation
/// options -- at the exact revision the snapshot was resolved on, after
/// cross-checking that the parse's recorded dependency set is exactly the
/// effective-source query and the options. When the RFC Analysis Lease and
/// `collectInputFrontier` land, this evolves into that machinery; the captured
/// stamp shape is the shape the frontier will produce.
///
/// A stamp is sealed on the resolving snapshot only, because the parse capability
/// memo is revision-local and is dropped at the next commit, so the dependency
/// cross-check is unrecoverable afterwards.
class SemanticSnapshotFreshness final {
public:
  SemanticSnapshotFreshness(SemanticSnapshotFreshness&&) noexcept = default;
  SemanticSnapshotFreshness& operator=(SemanticSnapshotFreshness&&) noexcept = default;
  ZC_DISALLOW_COPY(SemanticSnapshotFreshness);
  ~SemanticSnapshotFreshness() noexcept = default;

  /// \brief Seals a freshness stamp for `key` on the resolving snapshot.
  ///
  /// Derives the source and options input keys from the snapshot key, then
  /// cross-checks that `ParseSourceQuery`'s recorded dependencies on this
  /// snapshot are exactly the effective-source query and the compilation options.
  /// Stamps the four source-determining inputs (workspace source, editor overlay,
  /// selection, options); the overlay and selection may legitimately be absent
  /// under the workspace default. Fails closed (returns none) when the source key
  /// does not decode, the workspace source or options metadata is absent, or the
  /// recorded dependency set is not exactly the expected pair, so the caller
  /// degrades rather than trusting an unvalidated stamp.
  ///
  /// \param snapshot The snapshot the semantic snapshot was resolved on.
  /// \param key The snapshot key identifying the source content.
  /// \return The sealed freshness stamp, or none on any inconsistency.
  ZC_NODISCARD static zc::Maybe<SemanticSnapshotFreshness> seal(
      const query::QuerySnapshot& snapshot, const SemanticSnapshotKey& key);

  /// \brief Whether the tracked inputs are unchanged as of `current`.
  ///
  /// Re-reads every source-determining input's metadata on `current` and reports
  /// fresh only when all of them retain their sealed presence and change
  /// revision. The tracked set is the four inputs that determine the effective
  /// source and the parse: the workspace source snapshot, the editor overlay, the
  /// source selection, and the compilation options. Tracking all four (rather
  /// than only the derived effective-source query, whose changedAt moves only on
  /// re-evaluation) makes an overlay-bytes or selection change go stale. This is a
  /// conservative single-hop over-approximation: an overlay edit while the
  /// selection reads the workspace still marks stale even though the effective
  /// source is unchanged, which is safe (never fresh when the source could have
  /// changed) but not minimal. It is not the RFC's atomic frontier validation.
  ///
  /// \param current A later (or equal) snapshot to compare against.
  /// \return true when every tracked input is unchanged since sealing.
  ZC_NODISCARD bool isFreshAgainst(const query::QuerySnapshot& current) const;

  /// \brief The revision the snapshot was resolved on.
  ZC_NODISCARD query::DatabaseRevision resolvedRevision() const noexcept {
    return resolvedRevisionValue;
  }

private:
  SemanticSnapshotFreshness(query::DatabaseRevision resolvedRevision,
                            identity::source_query::StableSourceQueryKey&& sourceKey,
                            identity::CrateKey&& crate, SemanticSnapshotInputStamp source,
                            SemanticSnapshotInputStamp overlay,
                            SemanticSnapshotInputStamp selection,
                            SemanticSnapshotInputStamp options) noexcept
      : resolvedRevisionValue(resolvedRevision),
        sourceKeyValue(zc::mv(sourceKey)),
        crateValue(zc::mv(crate)),
        sourceStampValue(source),
        overlayStampValue(overlay),
        selectionStampValue(selection),
        optionsStampValue(options) {}

  query::DatabaseRevision resolvedRevisionValue;
  identity::source_query::StableSourceQueryKey sourceKeyValue;
  identity::CrateKey crateValue;
  SemanticSnapshotInputStamp sourceStampValue;
  SemanticSnapshotInputStamp overlayStampValue;
  SemanticSnapshotInputStamp selectionStampValue;
  SemanticSnapshotInputStamp optionsStampValue;
};

/// \brief One resolved semantic snapshot paired with its freshness stamp.
struct TrackedSemanticSnapshot final {
  SemanticSnapshot snapshot;
  /// \brief The freshness stamp, or none when it could not be sealed (the
  /// snapshot is then Unavailable and must not be treated as fresh).
  zc::Maybe<SemanticSnapshotFreshness> freshness;
};

/// \brief Resolves a semantic snapshot together with a single-hop freshness stamp.
///
/// Resolves the snapshot exactly as `resolveSemanticSnapshot`, then seals a
/// `SemanticSnapshotFreshness` on the same resolving snapshot for a published or
/// source-rejected result. When sealing fails its cross-check, the snapshot is
/// replaced with `Unavailable(EvaluationRejected)` and the freshness is none, so
/// a caller never receives a result it cannot later check for staleness. This
/// does not change the contract of `resolveSemanticSnapshot`.
///
/// \param database The query database to resolve against.
/// \param key The snapshot key.
/// \return The snapshot and its freshness stamp.
ZC_NODISCARD TrackedSemanticSnapshot resolveSemanticSnapshotTracked(query::QueryDatabase& database,
                                                                    const SemanticSnapshotKey& key);

/// \brief Cancellation-aware overload of `resolveSemanticSnapshotTracked`.
///
/// Identical to the token-less overload, but resolves the snapshot with the
/// caller's `CancellationSource::Token` so a cancellation surfaces as
/// `Unavailable(Cancelled)` (with no freshness stamp) instead of a stamped
/// result. The token-less overload keeps its prior behavior.
///
/// \param cancellation The caller's cancellation token, threaded to the resolve.
ZC_NODISCARD TrackedSemanticSnapshot
resolveSemanticSnapshotTracked(query::QueryDatabase& database, const SemanticSnapshotKey& key,
                               const query::CancellationSource::Token& cancellation);

}  // namespace zomlang::compiler::ide
