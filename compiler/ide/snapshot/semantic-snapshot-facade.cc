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

#include "compiler/ide/snapshot/semantic-snapshot-facade.h"

#include "compiler/ast/generated/node-accessors.h"
#include "compiler/ast/tree.h"
#include "compiler/diagnostics/fact/diagnostic-fact.h"
#include "compiler/diagnostics/fact/diagnostic-materializer.h"
#include "compiler/ide/snapshot/snapshot-diagnostic.h"
#include "compiler/ide/snapshot/snapshot-outline.h"
#include "compiler/ide/snapshot/snapshot-token.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/identity/source/source-query-input.h"
#include "compiler/parser/query/canonical-parsed-source.h"
#include "compiler/parser/query/effective-source-query.h"
#include "compiler/parser/query/parse-source-query.h"
#include "compiler/source/manager.h"
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

zc::Array<SnapshotDiagnostic> projectResolvedDiagnostics(
    const diagnostics::ResolvedDiagnosticBatch& batch) {
  zc::Vector<SnapshotDiagnostic> projected(batch.size());
  for (const auto& diagnostic : batch.diagnostics()) {
    const auto projectLocation = [](const diagnostics::ResolvedDiagnosticLocation& location) {
      const auto& range = location.range();
      const SnapshotRange snapshotRange{range.byteStart, range.byteEnd, range.isTokenRange};
      if (location.origin() == diagnostics::DiagnosticFactOrigin::Document) {
        return SnapshotDiagnosticLocation::document(
            zc::heapArray<uint8_t>(location.documentIdentityBytes()), snapshotRange);
      }
      return SnapshotDiagnosticLocation::source(location.source().encode(), snapshotRange);
    };

    zc::Vector<SnapshotDiagnosticRelated> related(diagnostic.related().size());
    for (const auto& item : diagnostic.related()) {
      auto location = projectLocation(item.location);
      switch (item.role) {
        case diagnostics::DiagnosticSecondaryRole::Highlight:
          related.add(SnapshotDiagnosticRelated::highlight(zc::mv(location)));
          break;
        case diagnostics::DiagnosticSecondaryRole::Note:
          related.add(SnapshotDiagnosticRelated::note(ZC_ASSERT_NONNULL(item.code),
                                                      zc::mv(location), item.arguments.asPtr()));
          break;
        case diagnostics::DiagnosticSecondaryRole::PreviousDeclaration:
          related.add(SnapshotDiagnosticRelated::previousDeclaration(zc::mv(location)));
          break;
      }
    }
    projected.add(SnapshotDiagnostic::project(diagnostic.code(),
                                              projectLocation(diagnostic.primary()),
                                              diagnostic.arguments(), related.releaseAsArray()));
  }
  return projected.releaseAsArray();
}

zc::Maybe<zc::Array<SnapshotDiagnostic>> projectReconstructedRejectedFacts(
    const parser::ParseRejected& rejected) {
  if (rejected.facts().size() == 0) { return zc::Array<SnapshotDiagnostic>(); }
  const auto& source = rejected.facts()[0].occurrence().source();
  diagnostics::SourceDiagnosticProvenanceResolver resolver(source, rejected.provenance());
  auto materialized = diagnostics::materializeDiagnosticFacts(rejected.facts(), resolver);
  if (!materialized.is<diagnostics::ResolvedDiagnosticBatch>()) { return zc::none; }
  return projectResolvedDiagnostics(materialized.get<diagnostics::ResolvedDiagnosticBatch>());
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

// Projects a source-rejected parse with resolved ranges. The rejection channel
// carries no provenance, so this re-derives it by replaying the parse through
// reconstructParseRejection over the two committed inputs the parse read
// directly. Any missing input or reconstruction failure yields none, and the
// caller fails closed instead of publishing an unresolved diagnostic. Reading
// EffectiveSourceSnapshot is essential here: a rejected editor overlay must be
// reconstructed from the overlay bytes selected for the parse, never from the
// workspace fallback.
zc::Maybe<zc::Array<SnapshotDiagnostic>> projectRejectedFactsWithRanges(
    const query::QuerySnapshot& snapshot, const SemanticSnapshotKey& key,
    zc::ArrayPtr<const diagnostics::DiagnosticFact> expectedFacts) {
  identity::CanonicalDecoder decoder(key.sourceKey().canonicalSourceBytes());
  auto sourceFile = identity::SourceFileKey::decodeCanonical(decoder);
  if (sourceFile == zc::none || !decoder.finished()) { return zc::none; }
  auto crate = ZC_ASSERT_NONNULL(sourceFile).crate().clone();

  auto source = snapshot.get<parser::EffectiveSourceSnapshot>(key.sourceKey());
  auto options = probeInputValue<identity::source_query::CompilationOptionsInput>(snapshot, crate);
  if (source.isRuntimeFailure() || source.kind() != query::QueryValueKind::Value ||
      options == zc::none) {
    return zc::none;
  }

  auto rejected = parser::reconstructParseRejection(key.sourceKey(), ZC_ASSERT_NONNULL(options),
                                                    source.value(), expectedFacts);
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

// Projects the flat top-level declaration outline out of the parsed tree. It
// walks the source-file root's statement list, unwraps each StatementListItem to
// its item node, and emits one entry per item whose kind is a recognized
// top-level symbol declaration (projectOutlineCategory). Only the category and
// the item's half-open byte range cross the boundary -- no name, no SyntaxKind,
// no node id. A node whose range does not resolve to a valid in-bounds span on
// the parse's own source manager is skipped rather than emitted with a degenerate
// range, so a malformed range degrades the entry instead of the whole outline.
zc::Array<SnapshotOutlineEntry> projectOutline(const parser::CanonicalParsedSource& parsed) {
  const ast::Tree& tree = parsed.tree();
  const source::SourceManager& sources = parsed.sourceManager();
  const source::BufferId& buffer = parsed.buffer();
  const uint64_t sourceByteLength = parsed.sourceBytes().size();

  const ast::NodeId rootId = tree.root();
  if (!tree.contains(rootId)) { return zc::Array<SnapshotOutlineEntry>(); }
  const ast::Node& rootNode = tree.node(rootId);
  if (rootNode.kind != ast::SyntaxKind::SourceFile) { return zc::Array<SnapshotOutlineEntry>(); }

  // SourceFile schema: the statements NodeList occupies payload words 2 and 3.
  const ast::NodeList statements = ast::nodeListFromPayload(rootNode, 2, 3);
  if (!tree.contains(statements)) { return zc::Array<SnapshotOutlineEntry>(); }
  const auto items = tree.list(statements);

  zc::Vector<SnapshotOutlineEntry> projected(items.size());
  for (const ast::NodeId itemId : items) {
    if (!tree.contains(itemId)) { continue; }
    const ast::Node& listItem = tree.node(itemId);
    if (listItem.kind != ast::SyntaxKind::StatementListItem) { continue; }
    // StatementListItem schema: the wrapped item NodeId is payload word 0.
    const ast::NodeId declId = ast::nodeIdFromPayload(listItem.payload.words[0]);
    if (!tree.contains(declId)) { continue; }
    const ast::Node& decl = tree.node(declId);

    auto category = projectOutlineCategory(decl.kind);
    if (category == zc::none) { continue; }
    if (decl.range.isInvalid()) { continue; }
    const uint64_t byteStart = sources.getLocOffsetInBuffer(decl.range.getStart(), buffer);
    const uint64_t byteEnd = sources.getLocOffsetInBuffer(decl.range.getEnd(), buffer);
    if (byteStart > byteEnd || byteEnd > sourceByteLength) { continue; }
    projected.add(SnapshotOutlineEntry{ZC_ASSERT_NONNULL(category), byteStart, byteEnd});
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
    ZC_IF_SOME(ranged, projectRejectedFactsWithRanges(snapshot, key, facts)) {
      return SemanticSnapshot::sourceRejected(key.documentVersion(), zc::mv(ranged));
    }
    return SemanticSnapshot::unavailable(SnapshotUnavailableReason::EvaluationRejected);
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
  auto materialized = diagnostics::materializeDiagnosticFacts(parsed.facts(), resolver);
  if (!materialized.is<diagnostics::ResolvedDiagnosticBatch>()) {
    return SemanticSnapshot::unavailable(SnapshotUnavailableReason::EvaluationRejected);
  }
  return SemanticSnapshot::published(
      parsed.canonicalSourceKey(), parsed.sourceBytes().size(), key.documentVersion(),
      projectTokens(parsed.tokens(), parsed.sourceBytes().size()), projectOutline(parsed),
      projectResolvedDiagnostics(materialized.get<diagnostics::ResolvedDiagnosticBatch>()));
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
