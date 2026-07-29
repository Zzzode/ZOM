// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::binder {

/// \brief Detached stable header for one exact named definition.
struct DefinitionHeaderSyntax final {
  using Key = StableDefinitionQueryKey;
  using Value = BinderQueryResult<StableDefinitionHeader>;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "DefinitionHeaderSyntax"_zcc,
      "zom.query.definition-header-syntax"_zcc,
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

/// \brief Detached stable header for one exact implementation occurrence.
struct ImplementationOccurrenceHeaderSyntax final {
  using Key = StableImplementationOccurrenceQueryKey;
  using Value = BinderQueryResult<StableImplementationOccurrenceHeader>;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ImplementationOccurrenceHeaderSyntax"_zcc,
      "zom.query.implementation-occurrence-header-syntax"_zcc,
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

/// \brief Registers the stable header semantic projection descriptors.
ZC_NODISCARD bool registerStableHeaderSyntaxQueries(query::QueryDatabase& database);

}  // namespace zomlang::compiler::binder
