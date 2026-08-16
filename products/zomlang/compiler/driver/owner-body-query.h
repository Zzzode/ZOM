#pragma once

#include "zomlang/compiler/binder/module-binding-allocation-plan.h"
#include "zomlang/compiler/binder/module-skeleton-query.h"
#include "zomlang/compiler/binder/owner-body-query.h"
#include "zomlang/compiler/binder/owner-body-syntax.h"
#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"
#include "zomlang/compiler/driver/contextual-binding-key.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Canonical stable owner inventory for one active module.
struct ModuleBodyOwnersQuery final {
  using Key = ContextualModuleKey;
  using Value = binder::ModuleBodyOwners;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ModuleBodyOwnersQuery"_zcc,
      "zom.query.module-body-owners"_zcc,
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

/// \brief Closed semantic syntax projection for one module-or-definition body owner.
struct OwnerBodySyntaxQuery final {
  using Key = ContextualBodyOwnerKey;
  using Value = binder::OwnerBodySyntax;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "OwnerBodySyntaxQuery"_zcc,
      "zom.query.owner-body-syntax"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Evictable,
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

/// \brief Revision-local source, node, range, and path map for one body owner.
struct OwnerBodyProvenanceQuery final {
  using Key = ContextualBodyOwnerKey;
  using Capability = binder::OwnerBodyProvenance;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "OwnerBodyProvenanceQuery"_zcc,
      "zom.query.owner-body-provenance"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::SourceOrKey};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<OwnerBodyProvenanceQuery> provide(
      query::CapabilityQueryContext<OwnerBodyProvenanceQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<OwnerBodyProvenanceQuery>& context, const Key& key,
      const Capability& candidate);
};

}  // namespace zomlang::compiler::driver::incremental_binding_query

namespace zomlang::compiler::binder {

/// \brief Complete stable semantic binding projection for one contextual owner body.
struct BindOwnerBody final {
  using Key = driver::incremental_binding_query::ContextualBodyOwnerKey;
  using Value = BinderQueryResult<BoundOwnerBody>;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "BindOwnerBody"_zcc,
      "zom.query.bind-owner-body"_zcc,
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

/// \brief Deterministic dense allocation plan for all contextual module body facts.
struct ModuleBindingAllocationPlanQuery final {
  using Key = driver::incremental_binding_query::ContextualModuleKey;
  using Value = BinderQueryResult<ModuleBindingAllocationPlan>;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ModuleBindingAllocationPlanQuery"_zcc,
      "zom.query.module-binding-allocation-plan"_zcc,
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

}  // namespace zomlang::compiler::binder

namespace zomlang::compiler::query {

template <>
class CapabilityCandidateContract<driver::incremental_binding_query::OwnerBodyProvenanceQuery>
    final {
public:
  using Descriptor = driver::incremental_binding_query::OwnerBodyProvenanceQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityFailureContract<driver::incremental_binding_query::OwnerBodyProvenanceQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::OwnerBodyProvenanceQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::incremental_binding_query::OwnerBodyProvenanceQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::OwnerBodyProvenanceQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

}  // namespace zomlang::compiler::query
