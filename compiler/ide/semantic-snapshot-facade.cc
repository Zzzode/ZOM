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
#include "compiler/ide/snapshot-token.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/identity/source-query-input.h"
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

// Whether a resolved range is a usable span inside the source. A parse-error
// diagnostic is legitimately a zero-width point (byteStart == byteEnd marks a
// caret position at the error), so an empty range is accepted; an inverted or
// past-the-end range is treated as unresolved, so a malformed provenance record
// degrades to a rangeless diagnostic rather than surfacing an out-of-bounds range.
bool rangeIsWithinSource(const diagnostics::DiagnosticSourceRange& range,
                         uint64_t sourceByteLength) {
  return range.byteStart <= range.byteEnd && range.byteEnd <= sourceByteLength;
}

// Projects one reconstructed rejected parse. The ParseRejected facts and their
// provenance map are built together by reconstructParseRejection, so each fact is
// resolved against that same map with no cross-list correlation against the
// demand's diagnostics. A fact whose provenance is absent or out of bounds is
// projected without a range.
zc::Array<SnapshotDiagnostic> projectReconstructedRejectedFacts(
    const parser::ParseRejected& rejected) {
  const auto facts = rejected.facts();
  const auto& provenance = rejected.provenance();
  const uint64_t sourceByteLength = rejected.sourceByteLength();
  zc::Vector<SnapshotDiagnostic> projected(facts.size());
  for (const auto& fact : facts) {
    auto resolved = provenance.find(fact.primary());
    ZC_IF_SOME(range, resolved) {
      if (rangeIsWithinSource(range, sourceByteLength)) {
        projected.add(SnapshotDiagnostic::projectRanged(
            fact.code(), SnapshotRange{range.byteStart, range.byteEnd, range.isTokenRange},
            fact.arguments()));
        continue;
      }
    }
    projected.add(SnapshotDiagnostic::projectRangeless(fact.code(), fact.arguments()));
  }
  return projected.releaseAsArray();
}

// Reads a query input value from the snapshot, or none when it is not a committed
// present value. Never records a caller dependency.
template <typename Input>
zc::Maybe<typename Input::Value> probeInputValue(const query::QuerySnapshot& snapshot,
                                                 const typename Input::Key& key) {
  auto result = snapshot.probeInput<Input>(key);
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  return result.value().clone();
}

// Best-effort projection of a source-rejected parse with resolved ranges. The
// rejection channel carries no provenance, so this re-derives it by replaying the
// parse through reconstructParseRejection over the two committed inputs the parse
// read directly. Any missing input or reconstruction failure yields none, and the
// caller keeps the rangeless projection; the source-rejected arm is never lost.
zc::Maybe<zc::Array<SnapshotDiagnostic>> projectRejectedFactsWithRanges(
    const query::QuerySnapshot& snapshot, const SemanticSnapshotKey& key,
    zc::ArrayPtr<const diagnostics::DiagnosticFact> expectedFacts) {
  identity::CanonicalDecoder decoder(key.sourceKey().canonicalSourceBytes());
  auto sourceFile = identity::SourceFileKey::decodeCanonical(decoder);
  if (sourceFile == zc::none || !decoder.finished()) { return zc::none; }
  auto crate = ZC_ASSERT_NONNULL(sourceFile).crate().clone();

  auto source =
      probeInputValue<identity::source_query::SourceSnapshotInput>(snapshot, key.sourceKey());
  auto options = probeInputValue<identity::source_query::CompilationOptionsInput>(snapshot, crate);
  if (source == zc::none || options == zc::none) { return zc::none; }

  auto rejected = parser::reconstructParseRejection(key.sourceKey(), ZC_ASSERT_NONNULL(options),
                                                    ZC_ASSERT_NONNULL(source), expectedFacts);
  ZC_IF_SOME(value, rejected) { return projectReconstructedRejectedFacts(value); }
  return zc::none;
}

// Projects the parsed token stream into the IDE-safe SnapshotToken sequence.
// Each token's compiler-internal SyntaxKind is collapsed to a closed category and
// only its half-open byte range crosses the boundary. The end-of-file sentinel
// (always the last token of a successful parse) is dropped: it is a zero-width
// marker, not a lexeme a feature renders. The parse capability already validated
// the ranges (each byteEnd <= source length, non-decreasing starts), so this
// defensively re-clamps against the source length and skips any token that does
// not satisfy byteStart <= byteEnd <= sourceByteLength rather than emitting a
// degenerate span.
zc::Array<SnapshotToken> projectTokens(zc::ArrayPtr<const parser::CanonicalParsedToken> tokens,
                                       uint64_t sourceByteLength) {
  zc::Vector<SnapshotToken> projected(tokens.size());
  for (const auto& token : tokens) {
    if (token.kind == ast::SyntaxKind::EndOfFile) { continue; }
    if (token.byteStart > token.byteEnd || token.byteEnd > sourceByteLength) { continue; }
    projected.add(SnapshotToken{projectTokenCategory(token.kind), token.byteStart, token.byteEnd});
  }
  return projected.releaseAsArray();
}

// Projects a resolved parse-capability demand into a sanitized snapshot. The
// demand is moved in so its published lease stays alive for the projection; both
// the token-less and token-accepting overloads share this body and differ only
// in how they obtain the demand.
SemanticSnapshot projectParseDemand(
    query::CapabilityDemandResult<parser::ParseSourceQuery>&& demand,
    const query::QuerySnapshot& snapshot, const SemanticSnapshotKey& key) {
  if (demand.isRuntimeRejected()) {
    return SemanticSnapshot::unavailable(mapRuntimeFailure(demand.runtimeFailure()));
  }
  if (demand.isSourceRejected()) {
    auto facts = demand.diagnostics().values();
    // Best effort: recover source ranges by replaying the parse. On any failure,
    // keep the rangeless projection so the source-rejected arm is never lost.
    ZC_IF_SOME(ranged, projectRejectedFactsWithRanges(snapshot, key, facts)) {
      return SemanticSnapshot::sourceRejected(key.documentVersion(), zc::mv(ranged));
    }
    return SemanticSnapshot::sourceRejected(key.documentVersion(), projectRejectedFacts(facts));
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
                                     projectTokens(parsed.tokens(), parsed.sourceBytes().size()),
                                     projectPublishedFacts(parsed.facts(), resolver));
}

}  // namespace

SemanticSnapshot resolveSemanticSnapshot(const query::QuerySnapshot& snapshot,
                                         const SemanticSnapshotKey& key) {
  return projectParseDemand(snapshot.getCapability<parser::ParseSourceQuery>(key.sourceKey()),
                            snapshot, key);
}

SemanticSnapshot resolveSemanticSnapshot(const query::QuerySnapshot& snapshot,
                                         const SemanticSnapshotKey& key,
                                         const query::CancellationSource::Token& cancellation) {
  return projectParseDemand(
      snapshot.getCapability<parser::ParseSourceQuery>(key.sourceKey(), cancellation), snapshot,
      key);
}

SemanticSnapshot resolveSemanticSnapshot(query::QueryDatabase& database,
                                         const SemanticSnapshotKey& key) {
  return resolveSemanticSnapshot(database.snapshot(), key);
}

SemanticSnapshot resolveSemanticSnapshot(query::QueryDatabase& database,
                                         const SemanticSnapshotKey& key,
                                         const query::CancellationSource::Token& cancellation) {
  return resolveSemanticSnapshot(database.snapshot(), key, cancellation);
}

}  // namespace zomlang::compiler::ide
