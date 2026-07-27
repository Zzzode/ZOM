#pragma once

#include "zomlang/compiler/binder/module-body-syntax.h"
#include "zomlang/compiler/binder/named-identity-inventory.h"
#include "zomlang/compiler/binder/revision-local-identity-sites.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Semantic stable named-definition membership for one selected module source.
struct NamedDefinitionInventoryQuery final {
  using Key = StableModuleQueryKey;
  using Value = binder::NamedDefinitionInventory;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::TypedQueryResult<Value> provide(query::QueryContext& context,
                                                             const Key& key);
  ZC_NODISCARD static bool verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result);
};

/// \brief Semantic stable implementation membership for one selected module source.
struct NamedImplementationInventoryQuery final {
  using Key = StableModuleQueryKey;
  using Value = binder::NamedImplementationInventory;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::TypedQueryResult<Value> provide(query::QueryContext& context,
                                                             const Key& key);
  ZC_NODISCARD static bool verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result);
};

/// \brief Revision-local stable definition sites for one selected module source.
struct RevisionLocalDefinitionSitesQuery final {
  using Key = StableModuleQueryKey;
  using Capability = binder::RevisionLocalDefinitionSites;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<Capability> provide(
      query::CapabilityQueryContext& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(query::CapabilityQueryContext& context,
                                                           const Key& key,
                                                           const Capability& candidate);
};

/// \brief Revision-local stable implementation sites for one selected module source.
struct RevisionLocalImplementationSitesQuery final {
  using Key = StableModuleQueryKey;
  using Capability = binder::RevisionLocalImplementationSites;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<Capability> provide(
      query::CapabilityQueryContext& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(query::CapabilityQueryContext& context,
                                                           const Key& key,
                                                           const Capability& candidate);
};

/// \brief Semantic detached module-owned syntax for one selected module source.
struct ModuleBodySyntaxQuery final {
  using Key = StableModuleQueryKey;
  using Value = binder::ModuleBodySyntax;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::TypedQueryResult<Value> provide(query::QueryContext& context,
                                                             const Key& key);
  ZC_NODISCARD static bool verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result);
};

/// \brief Revision-local module-body path and source provenance.
struct ModuleBodyProvenanceQuery final {
  using Key = StableModuleQueryKey;
  using Capability = binder::ModuleBodyProvenance;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<Capability> provide(
      query::CapabilityQueryContext& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(query::CapabilityQueryContext& context,
                                                           const Key& key,
                                                           const Capability& candidate);
};

}  // namespace zomlang::compiler::driver::incremental_binding_query
