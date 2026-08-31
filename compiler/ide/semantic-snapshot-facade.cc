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

#include "compiler/ide/semantic-snapshot-facade.h"

#include "compiler/diagnostics/fact/diagnostic-fact.h"
#include "compiler/diagnostics/fact/diagnostic-materializer.h"
#include "compiler/ide/snapshot-diagnostic.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/parser/query/canonical-parsed-source.h"
#include "compiler/parser/query/parse-source-query.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ide {
namespace {

// Maps a query runtime rejection onto the IDE-rail unavailable reason. A query
// runtime rejection is never a user diagnostic; the facade degrades the feature
// rather than publishing anything.
SnapshotUnavailableReason mapRuntimeFailure(query::QueryRuntimeFailure failure) noexcept {
  switch (failure) {
    case query::QueryRuntimeFailure::Cancelled:
      return SnapshotUnavailableReason::Cancelled;
    case query::QueryRuntimeFailure::MissingInput:
      return SnapshotUnavailableReason::MissingInput;
    case query::QueryRuntimeFailure::UnregisteredKind:
    case query::QueryRuntimeFailure::InvalidKeyEncoding:
    case query::QueryRuntimeFailure::ProviderRejected:
    case query::QueryRuntimeFailure::VerifierRejected:
    case query::QueryRuntimeFailure::Cycle:
    case query::QueryRuntimeFailure::FingerprintCollision:
    case query::QueryRuntimeFailure::InvariantViolation:
    case query::QueryRuntimeFailure::FinalSealRequired:
    case query::QueryRuntimeFailure::FinalSealMismatch:
    case query::QueryRuntimeFailure::AllocationFailure:
      return SnapshotUnavailableReason::EvaluationRejected;
  }
  return SnapshotUnavailableReason::EvaluationRejected;
}

// Projects the source-rejected fact sequence. The rejection channel carries no
// provenance map, so every diagnostic is rangeless.
zc::Array<SnapshotDiagnostic> projectRejectedFacts(
    zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  zc::Vector<SnapshotDiagnostic> projected(facts.size());
  for (const auto& fact : facts) {
    projected.add(SnapshotDiagnostic::projectRangeless(fact.code(), fact.arguments()));
  }
  return projected.releaseAsArray();
}

// Projects the published fact sequence, resolving each diagnostic's range from
// the parse provenance map. A fact whose provenance does not resolve is projected
// without a range rather than with a sentinel.
zc::Array<SnapshotDiagnostic> projectPublishedFacts(
    zc::ArrayPtr<const diagnostics::DiagnosticFact> facts,
    const diagnostics::SourceDiagnosticProvenanceResolver& resolver) {
  zc::Vector<SnapshotDiagnostic> projected(facts.size());
  for (const auto& fact : facts) {
    auto resolved = resolver.resolve(fact.primary());
    ZC_IF_SOME(range, resolved) {
      projected.add(SnapshotDiagnostic::projectRanged(
          fact.code(), SnapshotRange{range.byteStart, range.byteEnd, range.isTokenRange},
          fact.arguments()));
    } else {
      projected.add(SnapshotDiagnostic::projectRangeless(fact.code(), fact.arguments()));
    }
  }
  return projected.releaseAsArray();
}

}  // namespace

SemanticSnapshot resolveSemanticSnapshot(query::QueryDatabase& database,
                                         const SemanticSnapshotKey& key) {
  auto snapshot = database.snapshot();
  auto demand = snapshot.getCapability<parser::ParseSourceQuery>(key.sourceKey());

  if (demand.isRuntimeRejected()) {
    return SemanticSnapshot::unavailable(mapRuntimeFailure(demand.runtimeFailure()));
  }
  if (demand.isSourceRejected()) {
    return SemanticSnapshot::sourceRejected(key.documentVersion(),
                                            projectRejectedFacts(demand.diagnostics().values()));
  }
  if (!demand.isPublished()) {
    // A parse query has no key-rejection arm, so no other outcome is reachable;
    // degrade rather than abort if the evaluator ever adds one.
    return SemanticSnapshot::unavailable(SnapshotUnavailableReason::EvaluationRejected);
  }

  const parser::CanonicalParsedSource& parsed = demand.lease().capability();

  // Decode the source file identity the provenance resolver is scoped to. A
  // malformed or trailing-byte key is an internal inconsistency, not a user
  // diagnostic, so degrade to Unavailable rather than aborting.
  identity::CanonicalDecoder decoder(parsed.canonicalSourceKey());
  auto sourceFile = identity::SourceFileKey::decodeCanonical(decoder);
  if (sourceFile == zc::none || !decoder.finished()) {
    return SemanticSnapshot::unavailable(SnapshotUnavailableReason::EvaluationRejected);
  }

  diagnostics::SourceDiagnosticProvenanceResolver resolver(ZC_ASSERT_NONNULL(sourceFile),
                                                           parsed.provenance());
  return SemanticSnapshot::published(parsed.canonicalSourceKey(), parsed.sourceBytes().size(),
                                     key.documentVersion(),
                                     projectPublishedFacts(parsed.facts(), resolver));
}

}  // namespace zomlang::compiler::ide
