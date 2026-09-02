// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/identity/source-query-input.h"
#include "compiler/query/query-database.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::parser {

/// \brief The effective source bytes for one source file: the editor overlay when
/// one is selected, otherwise the workspace source.
///
/// RFC 0023 "IDE Semantic Snapshots": an editor overlay shadows the workspace
/// source bytes for the same source, and providers read the overlay only through
/// the ordinary source read. This derived query is that indirection: every parse
/// reads the effective source rather than the raw workspace input, so an overlay
/// replaces the parsed bytes without any producer or parser knowing about
/// overlays.
///
/// Migration-phase compatibility (default (i)): an ABSENT
/// `IdeSourceSelectionInput` is treated as `WorkspaceFile`, so every existing
/// producer and test that commits only the workspace source and options keeps
/// working unchanged. The RFC requirement that every source query first demand
/// the selection input is a later tightening and is NOT claimed here.
struct EffectiveSourceSnapshot final {
  using Key = identity::source_query::StableSourceQueryKey;
  using Value = identity::source_query::CanonicalSourceSnapshot;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "EffectiveSourceSnapshot"_zcc,
      "zom.query.effective-source-snapshot"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::TypedQueryResult<Value> provide(query::QueryContext& context,
                                                             const Key& key);
  ZC_NODISCARD static bool verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result);
};

/// \brief Registers the effective-source query descriptor.
ZC_NODISCARD bool registerEffectiveSourceQuery(query::QueryDatabase& database);

}  // namespace zomlang::compiler::parser
