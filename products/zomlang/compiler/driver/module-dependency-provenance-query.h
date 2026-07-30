// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/identity/module-resolution-key.h"
#include "zomlang/compiler/identity/source-key.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::module_graph_query {

/// \brief One current syntax occurrence backing a stable dependency request.
class ModuleDependencyProvenanceSite final {
public:
  ModuleDependencyProvenanceSite(ModuleDependencyProvenanceSite&&) noexcept = default;
  ModuleDependencyProvenanceSite& operator=(ModuleDependencyProvenanceSite&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencyProvenanceSite);

  ModuleDependencyProvenanceSite(uint32_t schemaPreorderOrdinal, ast::NodeId node,
                                 identity::SourceSpan&& span) noexcept;
  ZC_NODISCARD ModuleDependencyProvenanceSite clone() const;
  ZC_NODISCARD uint32_t schemaPreorderOrdinal() const noexcept;
  ZC_NODISCARD ast::NodeId node() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& span() const noexcept;

private:
  uint32_t schemaPreorderOrdinalValue;
  ast::NodeId nodeValue;
  identity::SourceSpan spanValue;
};

enum class ModuleDependencyProvenanceOriginKind : uint8_t { Source = 0x01, Prelude = 0x02 };

/// \brief Closed current-origin alternative for one stable dependency request.
class ModuleDependencyProvenanceOrigin final {
public:
  ModuleDependencyProvenanceOrigin(ModuleDependencyProvenanceOrigin&&) noexcept = default;
  ModuleDependencyProvenanceOrigin& operator=(ModuleDependencyProvenanceOrigin&&) noexcept =
      default;
  ZC_DISALLOW_COPY(ModuleDependencyProvenanceOrigin);

  ZC_NODISCARD static zc::Maybe<ModuleDependencyProvenanceOrigin> source(
      zc::Vector<ModuleDependencyProvenanceSite>&& sites);
  ZC_NODISCARD static ModuleDependencyProvenanceOrigin prelude();
  ZC_NODISCARD ModuleDependencyProvenanceOrigin clone() const;
  ZC_NODISCARD ModuleDependencyProvenanceOriginKind kind() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleDependencyProvenanceSite> sites() const noexcept;

private:
  ModuleDependencyProvenanceOrigin(ModuleDependencyProvenanceOriginKind kind,
                                   zc::Vector<ModuleDependencyProvenanceSite>&& sites) noexcept;

  ModuleDependencyProvenanceOriginKind kindValue;
  zc::Vector<ModuleDependencyProvenanceSite> siteValues;
};

/// \brief One stable request paired with its complete current origin.
class ModuleDependencyProvenanceEntry final {
public:
  ModuleDependencyProvenanceEntry(ModuleDependencyProvenanceEntry&&) noexcept = default;
  ModuleDependencyProvenanceEntry& operator=(ModuleDependencyProvenanceEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencyProvenanceEntry);

  ModuleDependencyProvenanceEntry(identity::ModuleResolutionKey&& request,
                                  ModuleDependencyProvenanceOrigin&& origin) noexcept;
  ZC_NODISCARD ModuleDependencyProvenanceEntry clone() const;
  ZC_NODISCARD const identity::ModuleResolutionKey& request() const noexcept;
  ZC_NODISCARD const ModuleDependencyProvenanceOrigin& origin() const noexcept;

private:
  identity::ModuleResolutionKey requestValue;
  ModuleDependencyProvenanceOrigin originValue;
};

/// \brief Runtime-only complete dependency provenance for one selected module.
class ModuleDependencyProvenanceMap final {
public:
  ModuleDependencyProvenanceMap(ModuleDependencyProvenanceMap&&) noexcept = default;
  ModuleDependencyProvenanceMap& operator=(ModuleDependencyProvenanceMap&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencyProvenanceMap);

  ZC_NODISCARD static zc::Maybe<ModuleDependencyProvenanceMap> from(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      const identity::Sha256Digest& sourceDigest,
      zc::Vector<ModuleDependencyProvenanceEntry>&& entries,
      const identity::Sha256Digest& stableWitness);
  ZC_NODISCARD ModuleDependencyProvenanceMap clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceDigest() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleDependencyProvenanceEntry> entries() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& stableWitness() const noexcept;
  ZC_NODISCARD bool sameAs(const ModuleDependencyProvenanceMap& other) const;

private:
  ModuleDependencyProvenanceMap(identity::ModuleKey&& module, identity::SourceFileKey&& source,
                                const identity::Sha256Digest& sourceDigest,
                                zc::Vector<ModuleDependencyProvenanceEntry>&& entries,
                                const identity::Sha256Digest& stableWitness) noexcept;

  identity::ModuleKey moduleValue;
  identity::SourceFileKey sourceValue;
  identity::Sha256Digest sourceDigestValue;
  zc::Vector<ModuleDependencyProvenanceEntry> entryValues;
  identity::Sha256Digest stableWitnessValue;
};

/// \brief Final-sealed retained dependency provenance for one selected module.
struct ModuleDependencyProvenanceQuery final {
  using Key = identity::ModuleKey;
  using Capability = ModuleDependencyProvenanceMap;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "ModuleDependencyProvenanceQuery"_zcc, "zom.query.module-dependency-provenance"_zcc,
      query::RetentionClass::Retained,       query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,         query::CapabilityAdmission::FinalSealedSnapshot};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<ModuleDependencyProvenanceQuery> provide(
      query::CapabilityQueryContext<ModuleDependencyProvenanceQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<ModuleDependencyProvenanceQuery>& context, const Key& key,
      const Capability& candidate);
};

}  // namespace zomlang::compiler::driver::module_graph_query

namespace zomlang::compiler::query {

template <>
class CapabilityCandidateContract<driver::module_graph_query::ModuleDependencyProvenanceQuery>
    final {
public:
  using Descriptor = driver::module_graph_query::ModuleDependencyProvenanceQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::ModuleDependencyProvenanceQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::module_graph_query::ModuleDependencyProvenanceQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::ModuleDependencyProvenanceQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::module_graph_query::ModuleDependencyProvenanceQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

}  // namespace zomlang::compiler::query
