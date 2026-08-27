#pragma once

#include "compiler/binder/surface/module-body-syntax.h"
#include "compiler/binder/identity/named-identity-inventory.h"
#include "compiler/binder/identity/revision-local-identity-sites.h"
#include "compiler/binder/stable/stable-binding-facts.h"
#include "compiler/diagnostics/fact/diagnostic-fact.h"
#include "compiler/driver/query/binding/incremental-binding-query-adapter.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Revision-local identity syntax provenance for one selected module source.
struct IdentitySyntaxSiteInventoryQuery final {
  using Key = StableModuleQueryKey;
  using Capability = binder::IdentitySyntaxSiteInventory;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "IdentitySyntaxSiteInventoryQuery"_zcc, "zom.query.identity-syntax-site-inventory"_zcc,
      query::RetentionClass::Retained,        query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,          query::CapabilityAdmission::AnySnapshot,
      query::FinalFailureProjection::None};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<IdentitySyntaxSiteInventoryQuery> provide(
      query::CapabilityQueryContext<IdentitySyntaxSiteInventoryQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<IdentitySyntaxSiteInventoryQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Revision-local verified stable identity candidates for one selected module source.
struct StableIdentityAdmissionQuery final {
  using Key = StableModuleQueryKey;
  using Capability = binder::StableIdentityAdmission;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "StableIdentityAdmissionQuery"_zcc, "zom.query.stable-identity-admission"_zcc,
      query::RetentionClass::Retained,    query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,      query::CapabilityAdmission::AnySnapshot,
      query::FinalFailureProjection::None};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<StableIdentityAdmissionQuery> provide(
      query::CapabilityQueryContext<StableIdentityAdmissionQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<StableIdentityAdmissionQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Semantic stable named-definition membership for one selected module source.
struct NamedDefinitionInventoryQuery final {
  using Key = StableModuleQueryKey;
  using Value = binder::NamedDefinitionInventory;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "NamedDefinitionInventoryQuery"_zcc,
      "zom.query.named-definition-inventory"_zcc,
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

/// \brief Semantic stable implementation membership for one selected module source.
struct NamedImplementationInventoryQuery final {
  using Key = StableModuleQueryKey;
  using Value = binder::NamedImplementationInventory;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "NamedImplementationInventoryQuery"_zcc,
      "zom.query.named-implementation-inventory"_zcc,
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

/// \brief Revision-local stable definition sites for one selected module source.
struct RevisionLocalDefinitionSitesQuery final {
  using Key = StableModuleQueryKey;
  using Capability = binder::RevisionLocalDefinitionSites;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "RevisionLocalDefinitionSitesQuery"_zcc,
      "zom.query.revision-local-definition-sites"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::SourceOrKey};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery> provide(
      query::CapabilityQueryContext<RevisionLocalDefinitionSitesQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<RevisionLocalDefinitionSitesQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Revision-local stable implementation sites for one selected module source.
struct RevisionLocalImplementationSitesQuery final {
  using Key = StableModuleQueryKey;
  using Capability = binder::RevisionLocalImplementationSites;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "RevisionLocalImplementationSitesQuery"_zcc,
      "zom.query.revision-local-implementation-sites"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::SourceOrKey};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>
  provide(query::CapabilityQueryContext<RevisionLocalImplementationSitesQuery>& context,
          const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<RevisionLocalImplementationSitesQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Semantic detached module-owned syntax for one selected module source.
struct ModuleBodySyntaxQuery final {
  using Key = StableModuleQueryKey;
  using Value = binder::ModuleBodySyntax;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ModuleBodySyntaxQuery"_zcc,
      "zom.query.module-body-syntax"_zcc,
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

/// \brief Revision-local module-body path and source provenance.
struct ModuleBodyProvenanceQuery final {
  using Key = StableModuleQueryKey;
  using Capability = binder::ModuleBodyProvenance;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "ModuleBodyProvenanceQuery"_zcc,
      "zom.query.module-body-provenance"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::SourceOrKey};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<ModuleBodyProvenanceQuery> provide(
      query::CapabilityQueryContext<ModuleBodyProvenanceQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<ModuleBodyProvenanceQuery>& context, const Key& key,
      const Capability& candidate);
};

}  // namespace zomlang::compiler::driver::incremental_binding_query

namespace zomlang::compiler::query {

template <>
class CapabilityCandidateContract<
    driver::incremental_binding_query::IdentitySyntaxSiteInventoryQuery>
    final {
public:
  using Descriptor = driver::incremental_binding_query::IdentitySyntaxSiteInventoryQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityFailureContract<driver::incremental_binding_query::IdentitySyntaxSiteInventoryQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::IdentitySyntaxSiteInventoryQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::incremental_binding_query::IdentitySyntaxSiteInventoryQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::IdentitySyntaxSiteInventoryQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

template <>
class CapabilityCandidateContract<driver::incremental_binding_query::StableIdentityAdmissionQuery>
    final {
public:
  using Descriptor = driver::incremental_binding_query::StableIdentityAdmissionQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityFailureContract<driver::incremental_binding_query::StableIdentityAdmissionQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::StableIdentityAdmissionQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::incremental_binding_query::StableIdentityAdmissionQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::StableIdentityAdmissionQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

template <>
class CapabilityCandidateContract<
    driver::incremental_binding_query::RevisionLocalDefinitionSitesQuery>
    final {
public:
  using Descriptor = driver::incremental_binding_query::RevisionLocalDefinitionSitesQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityFailureContract<
    driver::incremental_binding_query::RevisionLocalDefinitionSitesQuery,
    SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::RevisionLocalDefinitionSitesQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<
    driver::incremental_binding_query::RevisionLocalDefinitionSitesQuery,
    KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::RevisionLocalDefinitionSitesQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

template <>
class CapabilityCandidateContract<
    driver::incremental_binding_query::RevisionLocalImplementationSitesQuery>
    final {
public:
  using Descriptor = driver::incremental_binding_query::RevisionLocalImplementationSitesQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityFailureContract<
    driver::incremental_binding_query::RevisionLocalImplementationSitesQuery,
    SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::RevisionLocalImplementationSitesQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<
    driver::incremental_binding_query::RevisionLocalImplementationSitesQuery,
    KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::RevisionLocalImplementationSitesQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

template <>
class CapabilityCandidateContract<driver::incremental_binding_query::ModuleBodyProvenanceQuery>
    final {
public:
  using Descriptor = driver::incremental_binding_query::ModuleBodyProvenanceQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityFailureContract<driver::incremental_binding_query::ModuleBodyProvenanceQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::ModuleBodyProvenanceQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::incremental_binding_query::ModuleBodyProvenanceQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::incremental_binding_query::ModuleBodyProvenanceQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

}  // namespace zomlang::compiler::query
