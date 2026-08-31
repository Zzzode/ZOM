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

namespace zomlang::compiler::ide {

/// \brief Resolves one IDE semantic snapshot from the live parse query.
///
/// RFC 0023 "IDE Semantic Snapshots": the editor semantic facade is the only API
/// exposed to IDE features, and no conversion runs from an IDE value to a
/// verified compiler capability. This resolves the retained `ParseSourceQuery`
/// capability for the snapshot key's source content over one committed database
/// snapshot and projects it to a sanitized `SemanticSnapshot`:
///
///  - a published parse becomes `Published` with warning-severity diagnostics
///    whose ranges are resolved from the parse's provenance map (a diagnostic
///    without resolvable provenance is projected without a range, never with a
///    sentinel);
///  - a source-rejected parse becomes `SourceRejected` with error-severity
///    diagnostics that currently carry no range, because the rejection channel
///    carries no provenance map;
///  - every runtime rejection, and every internal decode or provenance failure,
///    becomes `Unavailable` with a closed reason.
///
/// The call never throws for a query outcome, never aborts, and never returns a
/// compiler handle. The document version from `key` labels the published and
/// source-rejected arms.
///
/// \param database The query database to demand the parse capability from.
/// \param key The snapshot key binding the source content to a document version.
/// \return The sanitized three-arm semantic snapshot.
ZC_NODISCARD SemanticSnapshot resolveSemanticSnapshot(query::QueryDatabase& database,
                                                      const SemanticSnapshotKey& key);

/// \brief Resolves one IDE semantic snapshot over an explicit committed snapshot.
///
/// Identical to the database overload, but demands the capability on the caller's
/// snapshot so a freshness stamp can be sealed on the exact same snapshot the
/// parse ran on (the parse capability memo is revision-local and its recorded
/// dependencies are only observable on that snapshot).
///
/// \param snapshot The committed snapshot to demand the parse capability on.
/// \param key The snapshot key binding the source content to a document version.
/// \return The sanitized three-arm semantic snapshot.
ZC_NODISCARD SemanticSnapshot resolveSemanticSnapshot(const query::QuerySnapshot& snapshot,
                                                      const SemanticSnapshotKey& key);

}  // namespace zomlang::compiler::ide
