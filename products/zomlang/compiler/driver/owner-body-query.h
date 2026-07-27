#pragma once

#include "zomlang/compiler/binder/owner-body-syntax.h"
#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Complete compilation context and stable body-owner query selector.
class ContextualBodyOwnerKey final {
public:
  ContextualBodyOwnerKey(ContextualBodyOwnerKey&&) noexcept = default;
  ContextualBodyOwnerKey& operator=(ContextualBodyOwnerKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ContextualBodyOwnerKey);

  ZC_NODISCARD static ContextualBodyOwnerKey from(CompilationRootSetQueryKey&& contextRoots,
                                                  binder::StableBodyOwnerKey&& owner);
  ZC_NODISCARD static zc::Maybe<ContextualBodyOwnerKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ContextualBodyOwnerKey clone() const;
  ZC_NODISCARD const CompilationRootSetQueryKey& contextRoots() const noexcept;
  ZC_NODISCARD const binder::StableBodyOwnerKey& owner() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ContextualBodyOwnerKey& other) const;
  bool operator!=(const ContextualBodyOwnerKey& other) const { return !(*this == other); }

private:
  ContextualBodyOwnerKey(CompilationRootSetQueryKey&& contextRoots,
                         binder::StableBodyOwnerKey&& owner) noexcept;

  CompilationRootSetQueryKey contextRootsField;
  binder::StableBodyOwnerKey ownerField;
};

/// \brief Canonical stable owner inventory for one active module.
struct ModuleBodyOwnersQuery final {
  using Key = ContextualModuleKey;
  using Value = binder::ModuleBodyOwners;

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

/// \brief Closed semantic syntax projection for one module-or-definition body owner.
struct OwnerBodySyntaxQuery final {
  using Key = ContextualBodyOwnerKey;
  using Value = binder::OwnerBodySyntax;

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

/// \brief Revision-local source, node, range, and path map for one body owner.
struct OwnerBodyProvenanceQuery final {
  using Key = ContextualBodyOwnerKey;
  using Capability = binder::OwnerBodyProvenance;

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
