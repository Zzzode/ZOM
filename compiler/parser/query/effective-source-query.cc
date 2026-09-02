// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/parser/query/effective-source-query.h"

namespace zomlang::compiler::parser {
namespace {

namespace sq = identity::source_query;

// Resolves the effective source for `key` on `context`: the editor overlay bytes
// when an overlay is selected, otherwise the workspace source. Shared by provide
// and verify so both derive the value identically. A runtime failure propagates;
// an unselectable source is a runtime MissingInput; absence means the selected
// source has no committed bytes.
query::TypedQueryResult<EffectiveSourceSnapshot::Value> resolveEffectiveSource(
    query::QueryContext& context, const EffectiveSourceSnapshot::Key& key) {
  // Probe (not get) the selection: an absent selection input must read as
  // Absence, not a MissingInput runtime failure, so the migration-phase
  // WorkspaceFile default applies when no editor has committed a selection.
  auto selection = context.probeInput<sq::IdeSourceSelectionInput>(key);
  if (selection.isRuntimeFailure()) {
    return query::TypedQueryResult<EffectiveSourceSnapshot::Value>::runtimeFailure(
        selection.runtimeFailure());
  }

  // Migration-phase compatibility (default (i)): an absent selection is treated
  // as WorkspaceFile so producers that commit only the workspace source keep
  // working. The RFC requirement to always demand a selection is a later
  // tightening and is not enforced here.
  bool useOverlay = false;
  zc::Maybe<identity::Sha256Digest> pinnedDigest;
  if (selection.kind() == query::QueryValueKind::Value) {
    const auto& value = selection.value();
    switch (value.kind()) {
      case sq::IdeSourceSelection::Kind::OpenOverlay:
        useOverlay = true;
        pinnedDigest = value.contentDigest();
        break;
      case sq::IdeSourceSelection::Kind::WorkspaceFile:
        pinnedDigest = value.contentDigest();
        break;
      case sq::IdeSourceSelection::Kind::Unavailable:
        return query::TypedQueryResult<EffectiveSourceSnapshot::Value>::runtimeFailure(
            query::QueryRuntimeFailure::MissingInput);
    }
  }

  auto source = useOverlay ? context.get<sq::EditorDocumentInput>(key)
                           : context.get<sq::SourceSnapshotInput>(key);
  if (source.isRuntimeFailure()) {
    return query::TypedQueryResult<EffectiveSourceSnapshot::Value>::runtimeFailure(
        source.runtimeFailure());
  }
  if (source.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<EffectiveSourceSnapshot::Value>::absence();
  }

  // When a selection pinned a content digest, the selected source bytes must
  // match it; a disagreement is a fail-closed rejection rather than a silently
  // stale result.
  ZC_IF_SOME(digest, pinnedDigest) {
    if (source.value().contentDigest() != digest) {
      return query::TypedQueryResult<EffectiveSourceSnapshot::Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
  }
  return query::TypedQueryResult<EffectiveSourceSnapshot::Value>::value(source.value().clone());
}

}  // namespace

zc::Array<uint8_t> EffectiveSourceSnapshot::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalSourceBytes());
}
zc::Maybe<EffectiveSourceSnapshot::Key> EffectiveSourceSnapshot::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return sq::StableSourceQueryKey::decodeBounded(bytes);
}
zc::Array<uint8_t> EffectiveSourceSnapshot::encodeValue(const Value& value) {
  return value.encodeCanonical();
}
zc::Maybe<EffectiveSourceSnapshot::Value> EffectiveSourceSnapshot::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return sq::CanonicalSourceSnapshot::decodeCanonical(bytes);
}

query::TypedQueryResult<EffectiveSourceSnapshot::Value> EffectiveSourceSnapshot::provide(
    query::QueryContext& context, const Key& key) {
  return resolveEffectiveSource(context, key);
}

bool EffectiveSourceSnapshot::verify(query::QueryContext& context, const Key& key,
                                     const query::TypedQueryResult<Value>& result) {
  auto recomputed = resolveEffectiveSource(context, key);
  if (recomputed.isRuntimeFailure()) { return false; }
  if (recomputed.kind() != result.kind()) { return false; }
  if (recomputed.kind() != query::QueryValueKind::Value) { return true; }
  return recomputed.value() == result.value();
}

bool registerEffectiveSourceQuery(query::QueryDatabase& database) {
  return database.registerDescriptor<EffectiveSourceSnapshot>().isRegistered();
}

}  // namespace zomlang::compiler::parser
