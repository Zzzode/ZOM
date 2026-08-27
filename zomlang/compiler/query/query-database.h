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

#pragma once

#include "zc/core/debug.h"
#include "zc/core/function.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/query/query-types.h"

namespace zomlang::compiler::basic {
class ThreadPool;
}

namespace zomlang::compiler::query {

class QueryContext;
template <typename Descriptor>
class CapabilityQueryContext;

template <typename Key>
struct ActiveMaterialization;

template <typename Descriptor, typename GlobalIdentityKey, typename MembershipDescriptor>
struct ActiveMaterializerPermission {
  static constexpr bool allowed = false;
};
class QueryDatabase;
class QuerySnapshot;

namespace _query_detail {
class CapabilityMemoBuilderBase;
class FinalSealPreparation;
class FinalSealPreparationResult;
class FinalSealPreparationState;
class VerifiedFinalSealAuthority;
}  // namespace _query_detail

/// \brief Typed deterministic provider or demand result.
template <typename T>
class TypedQueryResult final {
public:
  TypedQueryResult(TypedQueryResult&&) noexcept = default;
  TypedQueryResult& operator=(TypedQueryResult&&) noexcept = default;
  ZC_DISALLOW_COPY(TypedQueryResult);

  ZC_NODISCARD static TypedQueryResult value(T value) {
    zc::Maybe<T> retained(zc::mv(value));
    return TypedQueryResult(QueryValueKind::Value, zc::mv(retained), nullptr, zc::none);
  }
  ZC_NODISCARD static TypedQueryResult absence() {
    zc::Maybe<T> noValue;
    return TypedQueryResult(QueryValueKind::Absence, zc::mv(noValue), nullptr, zc::none);
  }
  ZC_NODISCARD static TypedQueryResult semanticFailure(zc::Array<uint8_t>&& bytes) {
    zc::Maybe<T> noValue;
    return TypedQueryResult(QueryValueKind::SemanticFailure, zc::mv(noValue), zc::mv(bytes),
                            zc::none);
  }
  ZC_NODISCARD static TypedQueryResult runtimeFailure(QueryRuntimeFailure failure) {
    zc::Maybe<T> noValue;
    zc::Maybe<QueryRuntimeFailure> retainedFailure(failure);
    return TypedQueryResult(QueryValueKind::Absence, zc::mv(noValue), nullptr,
                            zc::mv(retainedFailure));
  }

  ZC_NODISCARD bool isRuntimeFailure() const noexcept { return runtimeFailureField != zc::none; }
  ZC_NODISCARD QueryRuntimeFailure runtimeFailure() const {
    return ZC_REQUIRE_NONNULL(runtimeFailureField);
  }
  ZC_NODISCARD QueryValueKind kind() const noexcept { return kindField; }
  ZC_NODISCARD const T& value() const ZC_LIFETIMEBOUND { return ZC_REQUIRE_NONNULL(valueField); }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> semanticFailureBytes() const ZC_LIFETIMEBOUND {
    return semanticFailureBytesField.asPtr();
  }

private:
  TypedQueryResult(QueryValueKind kind, zc::Maybe<T>&& value,
                   zc::Array<uint8_t>&& semanticFailureBytes,
                   zc::Maybe<QueryRuntimeFailure>&& runtimeFailure) noexcept
      : kindField(kind),
        valueField(zc::mv(value)),
        semanticFailureBytesField(zc::mv(semanticFailureBytes)),
        runtimeFailureField(zc::mv(runtimeFailure)) {}

  QueryValueKind kindField;
  zc::Maybe<T> valueField;
  zc::Array<uint8_t> semanticFailureBytesField;
  zc::Maybe<QueryRuntimeFailure> runtimeFailureField;
};

/// \brief Closed provider outcomes for one revision-local capability descriptor.
enum class CapabilityProviderResultKind : uint8_t {
  Candidate = 0x01,
  SourceRejected = 0x02,
  KeyRejected = 0x03,
  RuntimeRejected = 0x04
};

namespace _query_detail {

template <typename Descriptor>
class CandidateCapabilityProvider final {
public:
  CandidateCapabilityProvider(zc::Own<typename Descriptor::Capability>&& candidate,
                              StableWitnessBytes&& stableWitness) noexcept
      : candidateField(zc::mv(candidate)), stableWitnessField(zc::mv(stableWitness)) {}

  zc::Own<typename Descriptor::Capability> candidateField;
  StableWitnessBytes stableWitnessField;
};

template <typename Diagnostic>
class SourceRejectedCapabilityProvider final {
public:
  explicit SourceRejectedCapabilityProvider(
      CanonicalNonEmptySequence<Diagnostic>&& diagnostics) noexcept
      : diagnosticsField(zc::mv(diagnostics)) {}

  CanonicalNonEmptySequence<Diagnostic> diagnosticsField;
};

template <typename KeyFailure>
class KeyRejectedCapabilityProvider final {
public:
  explicit KeyRejectedCapabilityProvider(KeyFailure&& failure) noexcept
      : failureField(zc::mv(failure)) {}

  KeyFailure failureField;
};

class RuntimeRejectedCapabilityProvider final {
public:
  explicit RuntimeRejectedCapabilityProvider(QueryRuntimeFailure failure) noexcept
      : failureField(failure) {}

  QueryRuntimeFailure failureField;
};

template <typename Alternative>
struct CapabilityProviderAlternative;

template <typename Diagnostic>
struct CapabilityProviderAlternative<SourceRejection<Diagnostic>> final {
  using Type = SourceRejectedCapabilityProvider<Diagnostic>;
};

template <typename KeyFailure>
struct CapabilityProviderAlternative<KeyRejection<KeyFailure>> final {
  using Type = KeyRejectedCapabilityProvider<KeyFailure>;
};

template <typename Descriptor, typename List>
struct CapabilityProviderStorage;

template <typename Descriptor, typename... Alternatives>
struct CapabilityProviderStorage<Descriptor, CapabilityFailureList<Alternatives...>> final {
  static_assert(CapabilityFailureListShape<CapabilityFailureList<Alternatives...>>::supported,
                "capability descriptor lists an unsupported failure alternative");
  static_assert(CapabilityFailureListShape<CapabilityFailureList<Alternatives...>>::sourceCount <=
                    1,
                "capability descriptor lists more than one source rejection");
  static_assert(CapabilityFailureListShape<CapabilityFailureList<Alternatives...>>::keyCount <= 1,
                "capability descriptor lists more than one key rejection");

  using Type = zc::OneOf<CandidateCapabilityProvider<Descriptor>,
                         typename CapabilityProviderAlternative<Alternatives>::Type...,
                         RuntimeRejectedCapabilityProvider>;
};

}  // namespace _query_detail

/// \brief Provider result for one descriptor-bound revision-local capability.
template <typename Descriptor>
class CapabilityProviderResult final {
public:
  using Capability = typename Descriptor::Capability;
  using FailureAlternatives = typename Descriptor::FailureAlternatives;

  CapabilityProviderResult(CapabilityProviderResult&&) noexcept = default;
  CapabilityProviderResult& operator=(CapabilityProviderResult&&) noexcept = default;
  ZC_DISALLOW_COPY(CapabilityProviderResult);

  ZC_NODISCARD static CapabilityProviderResult candidate(zc::Own<Capability>&& candidate,
                                                         StableWitnessBytes&& stableWitness) {
    ZC_IREQUIRE(candidate.get() != nullptr, "capability provider returned no candidate");
    return CapabilityProviderResult(CapabilityProviderResultKind::Candidate,
                                    _query_detail::CandidateCapabilityProvider<Descriptor>(
                                        zc::mv(candidate), zc::mv(stableWitness)));
  }

  template <typename Diagnostic>
    requires(_query_detail::CapabilityFailureListContains<SourceRejection<Diagnostic>,
                                                          FailureAlternatives>::value)
  ZC_NODISCARD static CapabilityProviderResult sourceRejected(
      CanonicalNonEmptySequence<Diagnostic>&& diagnostics) {
    return CapabilityProviderResult(
        CapabilityProviderResultKind::SourceRejected,
        _query_detail::SourceRejectedCapabilityProvider<Diagnostic>(zc::mv(diagnostics)));
  }

  template <typename KeyFailure>
    requires(_query_detail::CapabilityFailureListContains<KeyRejection<KeyFailure>,
                                                          FailureAlternatives>::value)
  ZC_NODISCARD static CapabilityProviderResult keyRejected(KeyFailure&& failure) {
    return CapabilityProviderResult(
        CapabilityProviderResultKind::KeyRejected,
        _query_detail::KeyRejectedCapabilityProvider<KeyFailure>(zc::mv(failure)));
  }

  ZC_NODISCARD static CapabilityProviderResult runtimeRejected(QueryRuntimeFailure failure) {
    return CapabilityProviderResult(CapabilityProviderResultKind::RuntimeRejected,
                                    _query_detail::RuntimeRejectedCapabilityProvider(failure));
  }

  ZC_NODISCARD CapabilityProviderResultKind kind() const noexcept { return kindField; }
  ZC_NODISCARD bool isCandidate() const noexcept {
    return kindField == CapabilityProviderResultKind::Candidate;
  }
  template <typename Alternatives = FailureAlternatives>
    requires(_query_detail::CapabilityFailureListShape<Alternatives>::sourceCount == 1)
  ZC_NODISCARD bool isSourceRejected() const noexcept {
    return kindField == CapabilityProviderResultKind::SourceRejected;
  }
  template <typename Alternatives = FailureAlternatives>
    requires(_query_detail::CapabilityFailureListShape<Alternatives>::keyCount == 1)
  ZC_NODISCARD bool isKeyRejected() const noexcept {
    return kindField == CapabilityProviderResultKind::KeyRejected;
  }
  ZC_NODISCARD bool isRuntimeRejected() const noexcept {
    return kindField == CapabilityProviderResultKind::RuntimeRejected;
  }

  ZC_NODISCARD QueryRuntimeFailure runtimeFailure() const {
    ZC_IREQUIRE(isRuntimeRejected(), "capability provider result has no runtime failure");
    return storageField.template get<_query_detail::RuntimeRejectedCapabilityProvider>()
        .failureField;
  }
  ZC_NODISCARD const Capability& candidate() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isCandidate(), "capability provider result has no candidate");
    return *storageField.template get<_query_detail::CandidateCapabilityProvider<Descriptor>>()
                .candidateField;
  }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> stableWitness() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isCandidate(), "capability provider result has no stable witness");
    return storageField.template get<_query_detail::CandidateCapabilityProvider<Descriptor>>()
        .stableWitnessField.bytes();
  }

  template <typename Alternatives = FailureAlternatives>
    requires(_query_detail::CapabilityFailureListShape<Alternatives>::sourceCount == 1)
  ZC_NODISCARD const auto& diagnostics() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isSourceRejected(), "capability provider result has no source diagnostics");
    using Diagnostic = typename _query_detail::CapabilityFailurePayloads<Alternatives>::Source;
    return storageField.template get<_query_detail::SourceRejectedCapabilityProvider<Diagnostic>>()
        .diagnosticsField;
  }

  template <typename Alternatives = FailureAlternatives>
    requires(_query_detail::CapabilityFailureListShape<Alternatives>::keyCount == 1)
  ZC_NODISCARD const auto& keyFailure() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isKeyRejected(), "capability provider result has no key failure");
    using KeyFailure = typename _query_detail::CapabilityFailurePayloads<Alternatives>::Key;
    return storageField.template get<_query_detail::KeyRejectedCapabilityProvider<KeyFailure>>()
        .failureField;
  }

  ZC_NODISCARD zc::Own<Capability> takeCandidate() && {
    ZC_IREQUIRE(isCandidate(), "capability provider result has no candidate");
    return zc::mv(storageField)
        .template get<_query_detail::CandidateCapabilityProvider<Descriptor>>()
        .candidateField;
  }

private:
  using Storage =
      typename _query_detail::CapabilityProviderStorage<Descriptor, FailureAlternatives>::Type;

  template <typename Alternative>
  CapabilityProviderResult(CapabilityProviderResultKind kind, Alternative&& alternative) noexcept
      : kindField(kind), storageField(zc::mv(alternative)) {}

  CapabilityProviderResultKind kindField;
  Storage storageField;
};

/// \brief Thread-safe cancellation authority for root demands.
class CancellationSource final {
public:
  CancellationSource();
  ~CancellationSource() noexcept(false);
  CancellationSource(CancellationSource&&) noexcept;
  CancellationSource& operator=(CancellationSource&&) noexcept;
  ZC_DISALLOW_COPY(CancellationSource);

  class Token;

  ZC_NODISCARD Token token() const;
  void cancel();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Cloneable read-only view of a cancellation source.
class CancellationSource::Token final {
public:
  Token(Token&&) noexcept;
  Token& operator=(Token&&) noexcept;
  ~Token() noexcept(false);
  ZC_DISALLOW_COPY(Token);

  ZC_NODISCARD Token clone() const;
  ZC_NODISCARD bool isCancelled() const;

private:
  struct Impl;
  explicit Token(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class CancellationSource;
  friend class QueryDatabase;
  friend class QuerySnapshot;
};

/// \brief Provider-only capability for tracked query reads.
class QueryContext final {
public:
  QueryContext(QueryContext&&) noexcept;
  QueryContext& operator=(QueryContext&&) noexcept;
  ~QueryContext() noexcept(false);
  ZC_DISALLOW_COPY(QueryContext);

  ZC_NODISCARD bool isCancelled() const;

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> get(const typename Spec::Key& key);

  template <typename Spec>
  ZC_NODISCARD CapabilityDemandResult<Spec> getCapability(const typename Spec::Key& key);

  /// \brief Reads one registered input and tracks its present or absent state.
  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> probeInput(const typename Spec::Key& key);

  /// \brief Demands one canonical dependency group concurrently.
  ///
  /// Providers reached from this group may issue sequential reads but cannot
  /// create another parallel group, which prevents bounded-pool starvation.
  /// Results retain the caller's key order even though dependency records use
  /// canonical query-key order.
  template <typename Spec>
  ZC_NODISCARD zc::Vector<TypedQueryResult<typename Spec::Value>> getParallel(
      zc::ArrayPtr<const typename Spec::Key> keys);

private:
  struct Impl;
  explicit QueryContext(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD QueryRequestResult getEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes);
  ZC_NODISCARD QueryRequestResult probeInputEncoded(zc::StringPtr domain,
                                                    zc::Array<uint8_t>&& keyBytes);
  ZC_NODISCARD zc::Vector<QueryRequestResult> getParallelEncoded(
      zc::StringPtr domain, zc::Vector<zc::Array<uint8_t>>&& keyBytes);
  ZC_NODISCARD QueryRequestResult
  publishCapability(zc::Own<_query_detail::CapabilityMemoBuilderBase>&& builder);
  ZC_NODISCARD zc::Maybe<QueryRuntimeFailure> inheritedFinalAdmissionFailure() const;
  ZC_NODISCARD FinalSnapshotClosureKind finalSnapshotClosureKind() const;
  ZC_NODISCARD zc::Maybe<QueryRuntimeFailure> capabilityPublicationFailure() const;
  ZC_NODISCARD const QueryDatabaseIdentity& databaseIdentity() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD DatabaseRevision snapshotRevision() const noexcept;
  ZC_NODISCARD const SemanticContextCapabilityResources& semanticContextResources() const
      ZC_LIFETIMEBOUND;

  zc::Own<Impl> impl;

  friend class QueryDatabase;
  template <typename Descriptor>
  friend class CapabilityQueryContext;
};

/// \brief Provider-only tracked read and materialization authority for capability queries.
///
/// Canonical Semantic and Persisted providers receive QueryContext and therefore cannot access
/// process-local semantic resources. Only RevisionLocal capability providers receive this type.
template <typename Descriptor>
class CapabilityQueryContext final {
public:
  ZC_DISALLOW_COPY_AND_MOVE(CapabilityQueryContext);

  ZC_NODISCARD bool isCancelled() const { return context.isCancelled(); }

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> get(const typename Spec::Key& key) {
    return context.get<Spec>(key);
  }

  template <typename Spec>
  ZC_NODISCARD CapabilityDemandResult<Spec> getCapability(const typename Spec::Key& key) {
    return context.getCapability<Spec>(key);
  }

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> probeInput(const typename Spec::Key& key) {
    return context.probeInput<Spec>(key);
  }

  /// \brief Returns the immutable revision captured by this capability evaluation.
  ZC_NODISCARD DatabaseRevision snapshotRevision() const noexcept {
    return context.snapshotRevision();
  }

  /// \brief Returns the independently verified state of the admitted final closure.
  ZC_NODISCARD FinalSnapshotClosureKind finalSnapshotClosureKind() const {
    return context.finalSnapshotClosureKind();
  }

  /// \brief Returns the descriptor's exact semantic-resource interface when available.
  template <typename Resource>
  ZC_NODISCARD zc::Maybe<const Resource&> semanticContextResources() const ZC_LIFETIMEBOUND {
    return zc::dynamicDowncastIfAvailable<const Resource>(context.semanticContextResources());
  }

  /// \brief Materializes one identity after exact active-membership admission.
  template <typename GlobalIdentityKey, typename MembershipDescriptor>
    requires(
        ActiveMaterializerPermission<Descriptor, GlobalIdentityKey, MembershipDescriptor>::allowed)
  ZC_NODISCARD TypedQueryResult<typename ActiveMaterialization<GlobalIdentityKey>::Handle>
  materializeActive(const typename MembershipDescriptor::Key& membershipKey,
                    const typename MembershipDescriptor::Record& expectedAuthority) {
    using Handle = typename ActiveMaterialization<GlobalIdentityKey>::Handle;
    static_assert(zc::isSameType<zc::RemoveConst<decltype(Descriptor::descriptor)>,
                                 CapabilityDescriptorMetadata>(),
                  "active materialization requires a capability descriptor");
    static_assert(Descriptor::descriptor.admission == CapabilityAdmission::FinalSealedSnapshot,
                  "active materialization requires final-sealed descriptor admission");
    ZC_IF_SOME(failure, context.inheritedFinalAdmissionFailure()) {
      return TypedQueryResult<Handle>::runtimeFailure(failure);
    }
    if (context.finalSnapshotClosureKind() == FinalSnapshotClosureKind::Failure) {
      return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
    }
    ZC_IF_SOME(failure, context.capabilityPublicationFailure()) {
      return TypedQueryResult<Handle>::runtimeFailure(failure);
    }
    auto globalKey = MembershipDescriptor::projectGlobalKey(membershipKey);
    if (globalKey == zc::none) {
      return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
    }
    auto membership = context.get<MembershipDescriptor>(membershipKey);
    if (membership.isRuntimeFailure()) {
      return TypedQueryResult<Handle>::runtimeFailure(membership.runtimeFailure());
    }
    if (membership.kind() != QueryValueKind::Value) {
      return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
    }
    if (!membership.value().isActive()) { return TypedQueryResult<Handle>::absence(); }
    if (!MembershipDescriptor::sameAuthority(membership.value().record(), expectedAuthority) ||
        !MembershipDescriptor::validateAuthority(membershipKey, ZC_REQUIRE_NONNULL(globalKey),
                                                 membership.value().record())) {
      return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
    }
    using Resource = typename ActiveMaterialization<GlobalIdentityKey>::Resource;
    auto resources = semanticContextResources<Resource>();
    if (resources == zc::none) {
      return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
    }
    return ActiveMaterialization<GlobalIdentityKey>::materialize(
        ZC_ASSERT_NONNULL(resources), ZC_REQUIRE_NONNULL(globalKey), membership.value().record());
  }

private:
  explicit CapabilityQueryContext(QueryContext& context) noexcept : context(context) {}
  QueryContext& context;

  friend class QueryDatabase;
};

/// \brief Compile-time shape of one canonical semantic query descriptor.
template <typename Descriptor>
concept SemanticQueryDescriptor =
    requires(QueryContext& context, const typename Descriptor::Key& key,
             const typename Descriptor::Value& value, zc::ArrayPtr<const uint8_t> bytes,
             const TypedQueryResult<typename Descriptor::Value>& result) {
      Descriptor::descriptor;
      Descriptor::encodeKey(key);
      Descriptor::decodeKey(bytes);
      Descriptor::encodeValue(value);
      Descriptor::decodeValue(bytes);
      Descriptor::provide(context, key);
      Descriptor::verify(context, key, result);
    } &&
    zc::isSameType<zc::RemoveConst<decltype(Descriptor::descriptor)>,
                   SemanticDescriptorMetadata>() &&
    zc::isSameType<decltype(Descriptor::encodeKey(zc::instance<const typename Descriptor::Key&>())),
                   zc::Array<uint8_t>>() &&
    zc::isSameType<decltype(Descriptor::decodeKey(zc::instance<zc::ArrayPtr<const uint8_t>>())),
                   zc::Maybe<typename Descriptor::Key>>() &&
    zc::isSameType<decltype(Descriptor::encodeValue(
                       zc::instance<const typename Descriptor::Value&>())),
                   zc::Array<uint8_t>>() &&
    zc::isSameType<decltype(Descriptor::decodeValue(zc::instance<zc::ArrayPtr<const uint8_t>>())),
                   zc::Maybe<typename Descriptor::Value>>() &&
    zc::isSameType<decltype(Descriptor::provide(zc::instance<QueryContext&>(),
                                                zc::instance<const typename Descriptor::Key&>())),
                   TypedQueryResult<typename Descriptor::Value>>() &&
    zc::isSameType<decltype(Descriptor::verify(
                       zc::instance<QueryContext&>(),
                       zc::instance<const typename Descriptor::Key&>(),
                       zc::instance<const TypedQueryResult<typename Descriptor::Value>&>())),
                   bool>();

/// \brief Compile-time shape of one revision-local capability descriptor.
template <typename Descriptor>
concept CapabilityQueryDescriptor =
    requires(CapabilityQueryContext<Descriptor>& context, const typename Descriptor::Key& key,
             const typename Descriptor::Capability& candidate, zc::ArrayPtr<const uint8_t> bytes) {
      typename Descriptor::FailureAlternatives;
      Descriptor::descriptor;
      Descriptor::encodeKey(key);
      Descriptor::decodeKey(bytes);
      Descriptor::provide(context, key);
      Descriptor::verify(context, key, candidate);
      CapabilityCandidateContract<Descriptor>::encode(candidate);
      CapabilityCandidateContract<Descriptor>::decode(bytes);
    } &&
    zc::isSameType<zc::RemoveConst<decltype(Descriptor::descriptor)>,
                   CapabilityDescriptorMetadata>() &&
    zc::isSameType<decltype(Descriptor::encodeKey(zc::instance<const typename Descriptor::Key&>())),
                   zc::Array<uint8_t>>() &&
    zc::isSameType<decltype(Descriptor::decodeKey(zc::instance<zc::ArrayPtr<const uint8_t>>())),
                   zc::Maybe<typename Descriptor::Key>>() &&
    zc::isSameType<decltype(Descriptor::provide(zc::instance<CapabilityQueryContext<Descriptor>&>(),
                                                zc::instance<const typename Descriptor::Key&>())),
                   CapabilityProviderResult<Descriptor>>() &&
    zc::isSameType<decltype(Descriptor::verify(
                       zc::instance<CapabilityQueryContext<Descriptor>&>(),
                       zc::instance<const typename Descriptor::Key&>(),
                       zc::instance<const typename Descriptor::Capability&>())),
                   zc::Maybe<zc::Array<uint8_t>>>() &&
    zc::isSameType<decltype(CapabilityCandidateContract<Descriptor>::encode(
                       zc::instance<const typename Descriptor::Capability&>())),
                   StableWitnessBytes>() &&
    zc::isSameType<decltype(CapabilityCandidateContract<Descriptor>::decode(
                       zc::instance<zc::ArrayPtr<const uint8_t>>())),
                   zc::Maybe<zc::Own<typename Descriptor::Capability>>>();

/// \brief Exact generated inventory role allowed to publish the final input seal.
template <typename Descriptor>
concept CompleteContextInventoryDescriptor =
    CompleteContextAuthorityInput<Descriptor> &&
    requires { QueryDescriptorInventoryBinding<Descriptor>::role; } &&
    QueryDescriptorInventoryBinding<Descriptor>::role ==
        QueryDescriptorRole::CompleteContextAuthority;

/// \brief Immutable view of one committed database revision.
class QuerySnapshot final {
public:
  QuerySnapshot(QuerySnapshot&&) noexcept;
  QuerySnapshot& operator=(QuerySnapshot&&) noexcept;
  ~QuerySnapshot() noexcept(false);
  ZC_DISALLOW_COPY(QuerySnapshot);

  ZC_NODISCARD DatabaseRevision revision() const noexcept;

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> get(
      const typename Spec::Key& key, const CancellationSource::Token& cancellation) const;

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> get(const typename Spec::Key& key) const;

  template <typename Spec>
  ZC_NODISCARD CapabilityDemandResult<Spec> getCapability(
      const typename Spec::Key& key, const CancellationSource::Token& cancellation) const;

  template <typename Spec>
  ZC_NODISCARD CapabilityDemandResult<Spec> getCapability(const typename Spec::Key& key) const;

  /// \brief Inspects one registered input without creating a caller dependency.
  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> probeInput(
      const typename Spec::Key& key, const CancellationSource::Token& cancellation) const;

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> probeInput(
      const typename Spec::Key& key) const;

  template <typename Spec>
  ZC_NODISCARD zc::Maybe<MemoMetadata> metadata(const typename Spec::Key& key) const;

  template <typename Spec>
  ZC_NODISCARD zc::Vector<DependencyGroup> dependencies(const typename Spec::Key& key) const;

  /// \brief Evicts an allowed memo value while retaining validation metadata.
  template <typename Spec>
  ZC_NODISCARD bool evictValue(const typename Spec::Key& key) const;

  /// \brief Reports whether an input or memo currently retains its complete value.
  template <typename Spec>
  ZC_NODISCARD bool hasRetainedValue(const typename Spec::Key& key) const;

  template <typename Spec>
  ZC_NODISCARD zc::Maybe<QueryKeyFingerprint> keyFingerprint(const typename Spec::Key& key) const;

  ZC_NODISCARD zc::Vector<QueryEvent> events() const;

private:
  struct Impl;
  explicit QuerySnapshot(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD QueryRequestResult getEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                             const CancellationSource::Token& cancellation) const;
  ZC_NODISCARD QueryRequestResult
  probeInputEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                    const CancellationSource::Token& cancellation) const;
  ZC_NODISCARD const QueryDatabaseIdentity& databaseIdentity() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Maybe<MemoMetadata> metadataEncoded(zc::StringPtr domain,
                                                       zc::Array<uint8_t>&& keyBytes) const;
  ZC_NODISCARD zc::Vector<DependencyGroup> dependenciesEncoded(zc::StringPtr domain,
                                                               zc::Array<uint8_t>&& keyBytes) const;
  ZC_NODISCARD bool evictValueEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes) const;
  ZC_NODISCARD bool hasRetainedValueEncoded(zc::StringPtr domain,
                                            zc::Array<uint8_t>&& keyBytes) const;
  ZC_NODISCARD zc::Maybe<QueryKeyFingerprint> keyFingerprintEncoded(
      zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes) const;

  zc::Own<Impl> impl;

  friend class QueryDatabase;
  friend class test::QueryRuntimeTestAccess;
};

/// \brief Exclusive private builder for the next complete explicit-input root.
class InputTransaction final {
public:
  InputTransaction(InputTransaction&&) noexcept;
  InputTransaction& operator=(InputTransaction&&) noexcept(false);
  ~InputTransaction() noexcept(false);
  ZC_DISALLOW_COPY(InputTransaction);

  template <typename Spec>
  InputMutationResult set(const typename Spec::Key& key, const typename Spec::Value& value);

  /// \brief Removes one staged mutable input from the next complete input root.
  template <typename Spec>
  InputMutationResult erase(const typename Spec::Key& key);

  ZC_NODISCARD InputCommitResult commit();
  void abandon();

private:
  struct Impl;
  explicit InputTransaction(zc::Own<Impl>&& impl) noexcept;
  InputMutationResult stageEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                   QueryValue&& value);
  InputMutationResult eraseEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes);

  zc::Own<Impl> impl;

  friend class QueryDatabase;
};

enum class InputTransactionOpenResultKind : uint8_t { Opened = 0x01, Rejected = 0x02 };

/// \brief Closed result of opening an exclusive input transaction.
class InputTransactionOpenResult final {
public:
  InputTransactionOpenResult(InputTransactionOpenResult&&) noexcept = default;
  InputTransactionOpenResult& operator=(InputTransactionOpenResult&&) noexcept = default;
  ZC_DISALLOW_COPY(InputTransactionOpenResult);

  ZC_NODISCARD static InputTransactionOpenResult opened(InputTransaction&& transaction) {
    return InputTransactionOpenResult(InputTransactionOpenResultKind::Opened, zc::mv(transaction));
  }
  ZC_NODISCARD static InputTransactionOpenResult rejected(InputTransactionFailure failure) {
    return InputTransactionOpenResult(InputTransactionOpenResultKind::Rejected, failure);
  }

  ZC_NODISCARD InputTransactionOpenResultKind kind() const noexcept { return kindField; }
  ZC_NODISCARD bool isOpened() const noexcept {
    return kindField == InputTransactionOpenResultKind::Opened;
  }
  ZC_NODISCARD InputTransaction takeTransaction() && {
    ZC_IREQUIRE(isOpened(), "rejected input transaction open has no transaction");
    return zc::mv(storageField).template get<InputTransaction>();
  }
  ZC_NODISCARD InputTransactionFailure failure() const {
    ZC_IREQUIRE(!isOpened(), "opened input transaction has no failure");
    return storageField.template get<InputTransactionFailure>();
  }

private:
  template <typename Alternative>
  InputTransactionOpenResult(InputTransactionOpenResultKind kind, Alternative&& alternative)
      : kindField(kind), storageField(zc::fwd<Alternative>(alternative)) {}

  InputTransactionOpenResultKind kindField;
  zc::OneOf<InputTransaction, InputTransactionFailure> storageField;
};

template <typename ContextRoots, typename FinalWitness = identity::Sha256Digest>
class SealedQuerySnapshot final {
public:
  SealedQuerySnapshot(SealedQuerySnapshot&&) noexcept = default;
  SealedQuerySnapshot& operator=(SealedQuerySnapshot&&) noexcept = default;
  ZC_DISALLOW_COPY(SealedQuerySnapshot);

  ZC_NODISCARD DatabaseRevision revision() const noexcept { return snapshotField.revision(); }
  ZC_NODISCARD const ContextRoots& contextRoots() const noexcept { return contextRootsField; }

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> get(const typename Spec::Key& key) const {
    return snapshotField.template get<Spec>(key);
  }

  template <typename Spec>
  ZC_NODISCARD CapabilityDemandResult<Spec> getCapability(const typename Spec::Key& key) const {
    return snapshotField.template getCapability<Spec>(key);
  }

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> probeInput(
      const typename Spec::Key& key) const {
    return snapshotField.template probeInput<Spec>(key);
  }

private:
  explicit SealedQuerySnapshot(QuerySnapshot&& snapshot, ContextRoots&& contextRoots) noexcept
      : snapshotField(zc::mv(snapshot)), contextRootsField(zc::mv(contextRoots)) {}

  QuerySnapshot snapshotField;
  ContextRoots contextRootsField;

  friend class QueryDatabase;
};

enum class SealedSnapshotResultKind : uint8_t { Admitted = 0x01, Rejected = 0x02 };

template <typename ContextRoots, typename FinalWitness = identity::Sha256Digest>
class SealedSnapshotResult final {
public:
  using Snapshot = SealedQuerySnapshot<ContextRoots, FinalWitness>;

  SealedSnapshotResult(SealedSnapshotResult&&) noexcept = default;
  SealedSnapshotResult& operator=(SealedSnapshotResult&&) noexcept = default;
  ZC_DISALLOW_COPY(SealedSnapshotResult);

  ZC_NODISCARD static SealedSnapshotResult admitted(Snapshot&& snapshot) {
    return SealedSnapshotResult(SealedSnapshotResultKind::Admitted, zc::mv(snapshot));
  }
  ZC_NODISCARD static SealedSnapshotResult rejected(QueryRuntimeFailure failure) {
    return SealedSnapshotResult(SealedSnapshotResultKind::Rejected, failure);
  }
  ZC_NODISCARD bool isAdmitted() const noexcept {
    return kindField == SealedSnapshotResultKind::Admitted;
  }
  ZC_NODISCARD Snapshot takeSnapshot() && {
    ZC_IREQUIRE(isAdmitted(), "rejected sealed snapshot result has no snapshot");
    return zc::mv(storageField).template get<Snapshot>();
  }
  ZC_NODISCARD QueryRuntimeFailure failure() const {
    ZC_IREQUIRE(!isAdmitted(), "admitted sealed snapshot has no failure");
    return storageField.template get<QueryRuntimeFailure>();
  }

private:
  template <typename Alternative>
  SealedSnapshotResult(SealedSnapshotResultKind kind, Alternative&& alternative)
      : kindField(kind), storageField(zc::fwd<Alternative>(alternative)) {}

  SealedSnapshotResultKind kindField;
  zc::OneOf<Snapshot, QueryRuntimeFailure> storageField;
};

namespace _query_detail {

class FinalSealPreparation final {
public:
  FinalSealPreparation(FinalSealPreparation&&) noexcept;
  FinalSealPreparation& operator=(FinalSealPreparation&&) noexcept;
  ~FinalSealPreparation() noexcept(false);
  ZC_DISALLOW_COPY(FinalSealPreparation);

private:
  FinalSealPreparation(QueryDatabaseIdentity&& database, DatabaseRevision revision,
                       CanonicalQueryKey&& contextKey,
                       zc::Arc<const FinalSealPreparationState>&& state) noexcept;

  QueryDatabaseIdentity databaseField;
  DatabaseRevision revisionField;
  CanonicalQueryKey contextKeyField;
  zc::Arc<const FinalSealPreparationState> stateField;

  friend class ::zomlang::compiler::query::QueryDatabase;
};

class FinalSealPreparationResult final {
public:
  FinalSealPreparationResult(FinalSealPreparationResult&&) noexcept = default;
  FinalSealPreparationResult& operator=(FinalSealPreparationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(FinalSealPreparationResult);

  ZC_NODISCARD static FinalSealPreparationResult prepared(FinalSealPreparation&& preparation) {
    return FinalSealPreparationResult(zc::mv(preparation));
  }
  ZC_NODISCARD static FinalSealPreparationResult rejected(InputTransactionFailure failure) {
    return FinalSealPreparationResult(failure);
  }
  ZC_NODISCARD bool isPrepared() const noexcept {
    return storageField.template is<FinalSealPreparation>();
  }
  ZC_NODISCARD FinalSealPreparation takePreparation() && {
    ZC_IREQUIRE(isPrepared(), "rejected final seal preparation has no preparation");
    return zc::mv(storageField).template get<FinalSealPreparation>();
  }
  ZC_NODISCARD InputTransactionFailure failure() const {
    ZC_IREQUIRE(!isPrepared(), "prepared final seal has no failure");
    return storageField.template get<InputTransactionFailure>();
  }

private:
  template <typename Alternative>
  explicit FinalSealPreparationResult(Alternative&& alternative)
      : storageField(zc::fwd<Alternative>(alternative)) {}

  zc::OneOf<FinalSealPreparation, InputTransactionFailure> storageField;
};

/// \brief Private proof that final-context authority was independently verified.
class VerifiedFinalSealAuthority final {
public:
  VerifiedFinalSealAuthority(VerifiedFinalSealAuthority&&) noexcept = default;
  VerifiedFinalSealAuthority& operator=(VerifiedFinalSealAuthority&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedFinalSealAuthority);

private:
  VerifiedFinalSealAuthority(QueryDatabaseIdentity&& database, DatabaseRevision revision,
                             CanonicalQueryKey&& contextKey, FinalSnapshotClosureKind closureKind,
                             zc::Array<uint8_t>&& finalWitness) noexcept
      : databaseField(zc::mv(database)),
        revisionField(revision),
        contextKeyField(zc::mv(contextKey)),
        closureKindField(closureKind),
        finalWitnessField(zc::mv(finalWitness)) {}

  QueryDatabaseIdentity databaseField;
  DatabaseRevision revisionField;
  CanonicalQueryKey contextKeyField;
  FinalSnapshotClosureKind closureKindField;
  zc::Array<uint8_t> finalWitnessField;

  friend class ::zomlang::compiler::query::QueryDatabase;
};

}  // namespace _query_detail

/// \brief Session-owned typed query registry, input root, memo graph, and flight authority.
class QueryDatabase final {
public:
  using ErasedKeyValidator = bool (*)(zc::ArrayPtr<const uint8_t>);
  using ErasedProvider =
      zc::Function<QueryRequestResult(QueryContext&, zc::ArrayPtr<const uint8_t>)>;
  using ErasedVerifier =
      zc::Function<bool(QueryContext&, zc::ArrayPtr<const uint8_t>, const QueryValue&)>;
  using ErasedCapabilityEvaluator =
      zc::Function<QueryRequestResult(QueryContext&, zc::ArrayPtr<const uint8_t>)>;

  /// \brief Constructs a query database that borrows the session scheduler.
  ///
  /// The scheduler must outlive this database. QueryDatabase never creates or owns worker
  /// threads, so the compiler session remains the sole concurrency-budget authority.
  QueryDatabase(basic::ThreadPool& scheduler, QueryDescriptorInventoryRef inventory);
  QueryDatabase(basic::ThreadPool& scheduler, QueryDescriptorInventoryRef inventory,
                zc::Arc<SemanticContextCapabilityArena>&& capabilityArena);
  ~QueryDatabase() noexcept(false);
  QueryDatabase(QueryDatabase&&) noexcept;
  QueryDatabase& operator=(QueryDatabase&&) noexcept(false);
  ZC_DISALLOW_COPY(QueryDatabase);

  template <typename Spec>
  ZC_NODISCARD DescriptorRegistrationResult registerDescriptor();

  ZC_NODISCARD QuerySnapshot snapshot();
  ZC_NODISCARD InputTransactionOpenResult
  beginInputTransaction(DatabaseRevision expectedPreviousRevision);

  template <typename CompleteContextInput, typename FinalWitness>
    requires CompleteContextInventoryDescriptor<CompleteContextInput>
  ZC_NODISCARD FinalSealResult<typename CompleteContextInput::Key, FinalWitness> sealInputs(
      const QuerySnapshot& finalSnapshot, const typename CompleteContextInput::Key& contextRoots,
      const FinalWitness& finalWitness);

  template <typename CompleteContextInput, typename FinalWitness>
    requires CompleteContextInventoryDescriptor<CompleteContextInput>
  ZC_NODISCARD SealedSnapshotResult<typename CompleteContextInput::Key, FinalWitness>
  admitFinalSnapshot(
      QuerySnapshot&& snapshot,
      const FinalSnapshotSeal<typename CompleteContextInput::Key, FinalWitness>& seal);

private:
  struct Impl;
  ZC_NODISCARD DescriptorRegistrationResult
  installDescriptor(zc::StringPtr inventoryIdentity, const QueryDescriptorInventoryRow& descriptor,
                    ErasedKeyValidator&& keyValidator, zc::Maybe<ErasedProvider>&& provider,
                    zc::Maybe<ErasedVerifier>&& verifier,
                    zc::Maybe<ErasedCapabilityEvaluator>&& capabilityEvaluator);
  ZC_NODISCARD _query_detail::FinalSealPreparationResult prepareFinalSeal(
      const QuerySnapshot& snapshot, zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes);
  ZC_NODISCARD zc::Maybe<InputTransactionFailure> publishFinalSeal(
      _query_detail::FinalSealPreparation&& preparation,
      _query_detail::VerifiedFinalSealAuthority&& authority,
      zc::ArrayPtr<const uint8_t> suppliedWitness);
  ZC_NODISCARD InputTransactionFailure
  rejectFinalSeal(_query_detail::FinalSealPreparation&& preparation);
  ZC_NODISCARD zc::Maybe<QueryRuntimeFailure> validateSnapshotAdmission(
      QuerySnapshot& snapshot, const QueryDatabaseIdentity& sealDatabase,
      DatabaseRevision sealRevision, zc::StringPtr descriptorDomain,
      zc::Array<uint8_t>&& contextKeyBytes, FinalSnapshotClosureKind closureKind,
      zc::ArrayPtr<const uint8_t> finalWitness);
  void armFinalSealPhaseTwoGateForTest();
  void waitForFinalSealPhaseTwoGateForTest();
  void releaseFinalSealPhaseTwoGateForTest();
  void pauseAtFinalSealPhaseTwoGate();

  zc::Own<Impl> impl;

  friend class QueryContext;
  friend class QuerySnapshot;
  friend class InputTransaction;
  friend class test::QueryRuntimeTestAccess;
};

namespace _query_detail {

template <typename Spec>
TypedQueryResult<typename Spec::Value> decodeResult(QueryRequestResult&& result);

template <typename Spec>
QueryRequestResult encodeResult(TypedQueryResult<typename Spec::Value>&& result);

/// \brief Internal construction and observation boundary for erased query results.
class QueryRequestResultAccess final {
private:
  ZC_NODISCARD static QueryRequestResult semantic(QueryValue&& value) {
    return QueryRequestResult(zc::mv(value));
  }

  ZC_NODISCARD static QueryRequestResult capabilityPublished(
      zc::Arc<RevisionLocalCapabilityMemoBase>&& memo) {
    return QueryRequestResult(zc::mv(memo));
  }

  ZC_NODISCARD static QueryRequestResult capabilityRejected(CapabilityFailureEnvelope&& rejection) {
    return QueryRequestResult(zc::mv(rejection));
  }

  ZC_NODISCARD static CapabilityFailureEnvelope verifiedCapabilityRejection(
      zc::StringPtr descriptorDomain, CapabilityFailureKind kind,
      zc::Array<uint8_t>&& canonicalPayload) {
    return CapabilityFailureEnvelope(zc::str(descriptorDomain), kind, zc::mv(canonicalPayload));
  }

  ZC_NODISCARD static QueryRequestResult runtimeRejected(QueryRuntimeFailure failure) {
    return QueryRequestResult(failure);
  }

  ZC_NODISCARD static bool isSemantic(const QueryRequestResult& result) {
    return result.storageField.template is<QueryValue>();
  }

  ZC_NODISCARD static bool isCapabilityPublished(const QueryRequestResult& result) {
    return result.storageField.template is<zc::Arc<RevisionLocalCapabilityMemoBase>>();
  }

  ZC_NODISCARD static bool isCapabilityRejected(const QueryRequestResult& result) {
    return result.storageField.template is<CapabilityFailureEnvelope>();
  }

  ZC_NODISCARD static bool isRuntimeRejected(const QueryRequestResult& result) {
    return result.storageField.template is<QueryRuntimeFailure>();
  }

  ZC_NODISCARD static const QueryValue& semanticValue(const QueryRequestResult& result) {
    return result.storageField.template get<QueryValue>();
  }

  ZC_NODISCARD static const RevisionLocalCapabilityMemoBase& capabilityMemo(
      const QueryRequestResult& result) {
    return *result.storageField.template get<zc::Arc<RevisionLocalCapabilityMemoBase>>().get();
  }

  ZC_NODISCARD static zc::Arc<RevisionLocalCapabilityMemoBase> retainCapabilityMemo(
      const QueryRequestResult& result) {
    return result.storageField.template get<zc::Arc<RevisionLocalCapabilityMemoBase>>().addRef();
  }

  ZC_NODISCARD static zc::Arc<RevisionLocalCapabilityMemoBase> takeCapabilityMemo(
      QueryRequestResult&& result) {
    return zc::mv(result.storageField).template get<zc::Arc<RevisionLocalCapabilityMemoBase>>();
  }

  ZC_NODISCARD static const CapabilityFailureEnvelope& capabilityRejection(
      const QueryRequestResult& result) {
    return result.storageField.template get<CapabilityFailureEnvelope>();
  }

  ZC_NODISCARD static QueryRuntimeFailure runtimeFailure(const QueryRequestResult& result) {
    return result.storageField.template get<QueryRuntimeFailure>();
  }

  ZC_NODISCARD static QueryRequestResult retain(const QueryRequestResult& result) {
    if (isSemantic(result)) { return semantic(semanticValue(result).clone()); }
    if (isCapabilityPublished(result)) { return capabilityPublished(retainCapabilityMemo(result)); }
    if (isCapabilityRejected(result)) {
      const auto& rejection = capabilityRejection(result);
      auto decoded = rejection.decodeCanonical();
      ZC_IREQUIRE(decoded != zc::none, "verified capability rejection lost canonical framing");
      return capabilityRejected(zc::mv(ZC_REQUIRE_NONNULL(decoded)));
    }
    return runtimeRejected(runtimeFailure(result));
  }

  ZC_NODISCARD static const QueryDatabaseIdentity& memoDatabase(
      const RevisionLocalCapabilityMemoBase& memo) {
    return memo.database();
  }

  ZC_NODISCARD static const CanonicalQueryKey& memoKey(
      const RevisionLocalCapabilityMemoBase& memo) {
    return memo.key();
  }

  ZC_NODISCARD static DatabaseRevision memoRevision(
      const RevisionLocalCapabilityMemoBase& memo) noexcept {
    return memo.revision();
  }

  ZC_NODISCARD static DatabaseRevision memoArenaRevision(
      const RevisionLocalCapabilityMemoBase& memo) noexcept {
    return memo.arena().revision();
  }

  ZC_NODISCARD static zc::ArrayPtr<const uint8_t> memoStableWitness(
      const RevisionLocalCapabilityMemoBase& memo) {
    return memo.stableWitness();
  }

  static void retainCapabilityDependency(
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>& retained,
      zc::Arc<RevisionLocalCapabilityMemoBase>&& dependency) {
    size_t position = 0;
    while (position < retained.size() &&
           memoKey(*retained[position].get()) < memoKey(*dependency.get())) {
      ++position;
    }
    if (position < retained.size() &&
        memoKey(*retained[position].get()) == memoKey(*dependency.get())) {
      return;
    }
    retained.add(zc::mv(dependency));
    for (size_t index = retained.size() - 1; index > position; --index) {
      auto temporary = zc::mv(retained[index]);
      retained[index] = zc::mv(retained[index - 1]);
      retained[index - 1] = zc::mv(temporary);
    }
  }

  friend class ::zomlang::compiler::query::QueryContext;
  friend class ::zomlang::compiler::query::QueryDatabase;
  friend class ::zomlang::compiler::query::QuerySnapshot;
  template <typename Spec>
  friend class ::zomlang::compiler::query::CapabilityResultDecoder;
  template <typename Spec>
  friend TypedQueryResult<typename Spec::Value> decodeResult(QueryRequestResult&& result);
  template <typename Spec>
  friend QueryRequestResult encodeResult(TypedQueryResult<typename Spec::Value>&& result);
};

class CapabilityMemoBuilderBase {
public:
  virtual ~CapabilityMemoBuilderBase() noexcept(false) = default;
  ZC_DISALLOW_COPY_AND_MOVE(CapabilityMemoBuilderBase);

  virtual zc::Arc<RevisionLocalCapabilityMemoBase> publish(
      QueryDatabaseIdentity&& database, CanonicalQueryKey&& key, DatabaseRevision revision,
      zc::Arc<SnapshotCapabilityArena>&& arena,
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies) = 0;

protected:
  CapabilityMemoBuilderBase() = default;
};

template <typename Descriptor>
class CapabilityMemoBuilder final : public CapabilityMemoBuilderBase {
public:
  using Capability = typename Descriptor::Capability;

  CapabilityMemoBuilder(zc::Own<Capability>&& candidate, zc::Array<uint8_t>&& stableWitness)
      : candidateField(zc::mv(candidate)), stableWitnessField(zc::mv(stableWitness)) {}

  zc::Arc<RevisionLocalCapabilityMemoBase> publish(
      QueryDatabaseIdentity&& database, CanonicalQueryKey&& key, DatabaseRevision revision,
      zc::Arc<SnapshotCapabilityArena>&& arena,
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies) override {
    return zc::arc<RevisionLocalCapabilityMemo<Capability>>(
        zc::mv(database), zc::mv(key), revision, zc::mv(arena), zc::mv(retainedDependencies),
        zc::mv(stableWitnessField), zc::mv(candidateField));
  }

private:
  zc::Own<Capability> candidateField;
  zc::Array<uint8_t> stableWitnessField;
};

template <typename Spec>
TypedQueryResult<typename Spec::Value> decodeResult(QueryRequestResult&& result) {
  using Value = typename Spec::Value;
  if (QueryRequestResultAccess::isRuntimeRejected(result)) {
    return TypedQueryResult<Value>::runtimeFailure(
        QueryRequestResultAccess::runtimeFailure(result));
  }
  if (!QueryRequestResultAccess::isSemantic(result)) {
    return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  const auto& erased = QueryRequestResultAccess::semanticValue(result);
  switch (erased.kind()) {
    case QueryValueKind::Value: {
      auto decoded = Spec::decodeValue(erased.canonicalBytes());
      ZC_IF_SOME(value, decoded) { return TypedQueryResult<Value>::value(zc::mv(value)); }
      return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
    }
    case QueryValueKind::Absence:
      return TypedQueryResult<Value>::absence();
    case QueryValueKind::SemanticFailure:
      return TypedQueryResult<Value>::semanticFailure(
          zc::heapArray<uint8_t>(erased.canonicalBytes()));
  }
  return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
}

template <typename Spec>
QueryRequestResult encodeResult(TypedQueryResult<typename Spec::Value>&& result) {
  if (result.isRuntimeFailure()) {
    return QueryRequestResultAccess::runtimeRejected(result.runtimeFailure());
  }
  switch (result.kind()) {
    case QueryValueKind::Value:
      return QueryRequestResultAccess::semantic(
          QueryValue::value(Spec::encodeValue(result.value())));
    case QueryValueKind::Absence:
      return QueryRequestResultAccess::semantic(QueryValue::absence());
    case QueryValueKind::SemanticFailure:
      return QueryRequestResultAccess::semantic(
          QueryValue::semanticFailure(zc::heapArray<uint8_t>(result.semanticFailureBytes())));
  }
  return QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> decodeValueForVerifier(const QueryValue& value) {
  using Value = typename Spec::Value;
  switch (value.kind()) {
    case QueryValueKind::Value: {
      auto decoded = Spec::decodeValue(value.canonicalBytes());
      ZC_IF_SOME(decodedValue, decoded) {
        return TypedQueryResult<Value>::value(zc::mv(decodedValue));
      }
      return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
    }
    case QueryValueKind::Absence:
      return TypedQueryResult<Value>::absence();
    case QueryValueKind::SemanticFailure:
      return TypedQueryResult<Value>::semanticFailure(
          zc::heapArray<uint8_t>(value.canonicalBytes()));
  }
  return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
}

inline bool canonicalBytesEqual(zc::ArrayPtr<const uint8_t> left,
                                zc::ArrayPtr<const uint8_t> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

}  // namespace _query_detail

template <typename Spec>
class CapabilityResultDecoder final {
private:
  using Capability = typename Spec::Capability;
  using Lease = QueryCapabilityLease<const Capability>;
  using Result = CapabilityDemandResult<Spec>;
  using FailureAlternatives = typename Spec::FailureAlternatives;

  ZC_NODISCARD static Result decode(QueryRequestResult&& result,
                                    const QueryDatabaseIdentity& database,
                                    DatabaseRevision revision) {
    using Binding = QueryDescriptorInventoryBinding<Spec>;
    static_assert(requires {
      Binding::inventoryIdentity;
      Binding::ordinal;
      Binding::descriptorType;
      Binding::role;
      Binding::ownerPathFamily;
    });
    const QueryKindId descriptorKind(Binding::ordinal);
    if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(result)) {
      return Result::runtimeRejected(
          _query_detail::QueryRequestResultAccess::runtimeFailure(result));
    }
    if (_query_detail::QueryRequestResultAccess::isCapabilityPublished(result)) {
      const auto& memo = _query_detail::QueryRequestResultAccess::capabilityMemo(result);
      if (_query_detail::QueryRequestResultAccess::memoKey(memo).kind() != descriptorKind ||
          _query_detail::QueryRequestResultAccess::memoDatabase(memo) != database ||
          _query_detail::QueryRequestResultAccess::memoRevision(memo) != revision) {
        return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
      }
      auto erased = _query_detail::QueryRequestResultAccess::takeCapabilityMemo(zc::mv(result));
      auto typed = erased.template downcast<RevisionLocalCapabilityMemo<Capability>>();
      return Result::published(Lease(zc::mv(typed)));
    }
    if (!_query_detail::QueryRequestResultAccess::isCapabilityRejected(result)) {
      return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
    }
    const auto& encodedRejection =
        _query_detail::QueryRequestResultAccess::capabilityRejection(result);
    auto decodedRejection = encodedRejection.decodeCanonical();
    if (decodedRejection == zc::none) {
      return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
    }
    auto rejection = zc::mv(ZC_REQUIRE_NONNULL(decodedRejection));
    if (rejection.descriptorDomain() != Spec::descriptor.domain) {
      return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
    }
    switch (rejection.kind()) {
      case CapabilityFailureKind::SourceRejected:
        if constexpr (_query_detail::CapabilityFailureListShape<FailureAlternatives>::sourceCount ==
                      1) {
          using Diagnostic =
              typename _query_detail::CapabilityFailurePayloads<FailureAlternatives>::Source;
          using Alternative = SourceRejection<Diagnostic>;
          auto decoded =
              CapabilityFailureContract<Spec, Alternative>::decode(rejection.canonicalPayload());
          if (decoded == zc::none) {
            return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
          }
          auto canonical =
              CapabilityFailureContract<Spec, Alternative>::encode(ZC_REQUIRE_NONNULL(decoded));
          if (!_query_detail::canonicalBytesEqual(canonical.asPtr(),
                                                  rejection.canonicalPayload())) {
            return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
          }
          return Result::template sourceRejected<Diagnostic>(zc::mv(ZC_REQUIRE_NONNULL(decoded)));
        }
        break;
      case CapabilityFailureKind::KeyRejected:
        if constexpr (_query_detail::CapabilityFailureListShape<FailureAlternatives>::keyCount ==
                      1) {
          using KeyFailure =
              typename _query_detail::CapabilityFailurePayloads<FailureAlternatives>::Key;
          using Alternative = KeyRejection<KeyFailure>;
          auto decoded =
              CapabilityFailureContract<Spec, Alternative>::decode(rejection.canonicalPayload());
          if (decoded == zc::none) {
            return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
          }
          auto canonical =
              CapabilityFailureContract<Spec, Alternative>::encode(ZC_REQUIRE_NONNULL(decoded));
          if (!_query_detail::canonicalBytesEqual(canonical.asPtr(),
                                                  rejection.canonicalPayload())) {
            return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
          }
          return Result::template keyRejected<KeyFailure>(zc::mv(ZC_REQUIRE_NONNULL(decoded)));
        }
        break;
    }
    return Result::runtimeRejected(QueryRuntimeFailure::InvariantViolation);
  }

  friend class QueryContext;
  friend class QuerySnapshot;
  friend class test::QueryRuntimeTestAccess;
};

template <typename Spec>
TypedQueryResult<typename Spec::Value> QueryContext::get(const typename Spec::Key& key) {
  return _query_detail::decodeResult<Spec>(
      getEncoded(Spec::descriptor.domain, Spec::encodeKey(key)));
}

template <typename Spec>
CapabilityDemandResult<Spec> QueryContext::getCapability(const typename Spec::Key& key) {
  return CapabilityResultDecoder<Spec>::decode(
      getEncoded(Spec::descriptor.domain, Spec::encodeKey(key)), databaseIdentity(),
      snapshotRevision());
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QueryContext::probeInput(const typename Spec::Key& key) {
  return _query_detail::decodeResult<Spec>(
      probeInputEncoded(Spec::descriptor.domain, Spec::encodeKey(key)));
}

template <typename Spec>
zc::Vector<TypedQueryResult<typename Spec::Value>> QueryContext::getParallel(
    zc::ArrayPtr<const typename Spec::Key> keys) {
  zc::Vector<zc::Array<uint8_t>> encoded;
  for (const auto& key : keys) { encoded.add(Spec::encodeKey(key)); }
  auto erased = getParallelEncoded(Spec::descriptor.domain, zc::mv(encoded));
  zc::Vector<TypedQueryResult<typename Spec::Value>> result;
  for (auto& item : erased) { result.add(_query_detail::decodeResult<Spec>(zc::mv(item))); }
  return result;
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QuerySnapshot::get(
    const typename Spec::Key& key, const CancellationSource::Token& cancellation) const {
  return _query_detail::decodeResult<Spec>(
      getEncoded(Spec::descriptor.domain, Spec::encodeKey(key), cancellation));
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QuerySnapshot::get(const typename Spec::Key& key) const {
  CancellationSource cancellation;
  return get<Spec>(key, cancellation.token());
}

template <typename Spec>
CapabilityDemandResult<Spec> QuerySnapshot::getCapability(
    const typename Spec::Key& key, const CancellationSource::Token& cancellation) const {
  return CapabilityResultDecoder<Spec>::decode(
      getEncoded(Spec::descriptor.domain, Spec::encodeKey(key), cancellation), databaseIdentity(),
      revision());
}

template <typename Spec>
CapabilityDemandResult<Spec> QuerySnapshot::getCapability(const typename Spec::Key& key) const {
  CancellationSource cancellation;
  return getCapability<Spec>(key, cancellation.token());
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QuerySnapshot::probeInput(
    const typename Spec::Key& key, const CancellationSource::Token& cancellation) const {
  return _query_detail::decodeResult<Spec>(
      probeInputEncoded(Spec::descriptor.domain, Spec::encodeKey(key), cancellation));
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QuerySnapshot::probeInput(
    const typename Spec::Key& key) const {
  CancellationSource cancellation;
  return probeInput<Spec>(key, cancellation.token());
}

template <typename Spec>
zc::Maybe<MemoMetadata> QuerySnapshot::metadata(const typename Spec::Key& key) const {
  return metadataEncoded(Spec::descriptor.domain, Spec::encodeKey(key));
}

template <typename Spec>
zc::Vector<DependencyGroup> QuerySnapshot::dependencies(const typename Spec::Key& key) const {
  return dependenciesEncoded(Spec::descriptor.domain, Spec::encodeKey(key));
}

template <typename Spec>
bool QuerySnapshot::evictValue(const typename Spec::Key& key) const {
  return evictValueEncoded(Spec::descriptor.domain, Spec::encodeKey(key));
}

template <typename Spec>
bool QuerySnapshot::hasRetainedValue(const typename Spec::Key& key) const {
  return hasRetainedValueEncoded(Spec::descriptor.domain, Spec::encodeKey(key));
}

template <typename Spec>
zc::Maybe<QueryKeyFingerprint> QuerySnapshot::keyFingerprint(const typename Spec::Key& key) const {
  return keyFingerprintEncoded(Spec::descriptor.domain, Spec::encodeKey(key));
}

template <typename Spec>
InputMutationResult InputTransaction::set(const typename Spec::Key& key,
                                          const typename Spec::Value& value) {
  return stageEncoded(Spec::descriptor.domain, Spec::encodeKey(key),
                      QueryValue::value(Spec::encodeValue(value)));
}

template <typename Spec>
InputMutationResult InputTransaction::erase(const typename Spec::Key& key) {
  return eraseEncoded(Spec::descriptor.domain, Spec::encodeKey(key));
}

template <typename CompleteContextInput, typename FinalWitness>
  requires CompleteContextInventoryDescriptor<CompleteContextInput>
FinalSealResult<typename CompleteContextInput::Key, FinalWitness> QueryDatabase::sealInputs(
    const QuerySnapshot& finalSnapshot, const typename CompleteContextInput::Key& contextRoots,
    const FinalWitness& finalWitness) {
  using Result = FinalSealResult<typename CompleteContextInput::Key, FinalWitness>;
  auto preparation = prepareFinalSeal(finalSnapshot, CompleteContextInput::descriptor.domain,
                                      CompleteContextInput::encodeKey(contextRoots));
  if (!preparation.isPrepared()) { return Result::rejected(preparation.failure()); }

  pauseAtFinalSealPhaseTwoGate();
  auto authority = finalSnapshot.get<CompleteContextInput>(contextRoots);
  auto authorityCheck = FinalAuthorityCheck::Rejected;
  if (!authority.isRuntimeFailure() && authority.kind() == QueryValueKind::Value) {
    authorityCheck = CompleteContextInput::verifyFinalAuthority(finalSnapshot, contextRoots,
                                                                authority.value(), finalWitness);
  }

  auto prepared = zc::mv(preparation).takePreparation();
  if (authorityCheck == FinalAuthorityCheck::Rejected) {
    return Result::rejected(rejectFinalSeal(zc::mv(prepared)));
  }
  const auto closureKind = authorityCheck == FinalAuthorityCheck::VerifiedSuccess
                               ? FinalSnapshotClosureKind::Success
                               : FinalSnapshotClosureKind::Failure;

  _query_detail::VerifiedFinalSealAuthority verified(
      prepared.databaseField.retain(), prepared.revisionField, prepared.contextKeyField.clone(),
      closureKind, zc::heapArray<uint8_t>(finalWitness.bytes()));
  ZC_IF_SOME(failure, publishFinalSeal(zc::mv(prepared), zc::mv(verified), finalWitness.bytes())) {
    return Result::rejected(failure);
  }

  auto retainedRoots =
      CompleteContextInput::decodeKey(CompleteContextInput::encodeKey(contextRoots));
  ZC_IREQUIRE(retainedRoots != zc::none,
              "verified complete-context key did not survive canonical round trip");
  return Result::sealed(FinalSnapshotSeal<typename CompleteContextInput::Key, FinalWitness>(
      finalSnapshot.databaseIdentity().retain(), finalSnapshot.revision(),
      zc::mv(ZC_REQUIRE_NONNULL(retainedRoots)), closureKind, finalWitness));
}

template <typename CompleteContextInput, typename FinalWitness>
  requires CompleteContextInventoryDescriptor<CompleteContextInput>
SealedSnapshotResult<typename CompleteContextInput::Key, FinalWitness>
QueryDatabase::admitFinalSnapshot(
    QuerySnapshot&& snapshot,
    const FinalSnapshotSeal<typename CompleteContextInput::Key, FinalWitness>& seal) {
  using Result = SealedSnapshotResult<typename CompleteContextInput::Key, FinalWitness>;
  ZC_IF_SOME(failure,
             validateSnapshotAdmission(snapshot, seal.database(), seal.revision(),
                                       CompleteContextInput::descriptor.domain,
                                       CompleteContextInput::encodeKey(seal.contextRoots()),
                                       seal.closureKind(), seal.finalWitness().bytes())) {
    return Result::rejected(failure);
  }
  if constexpr (requires { seal.contextRoots().clone(); }) {
    auto retainedRoots = seal.contextRoots().clone();
    return Result::admitted(SealedQuerySnapshot<typename CompleteContextInput::Key, FinalWitness>(
        zc::mv(snapshot), zc::mv(retainedRoots)));
  } else {
    auto retainedRoots = seal.contextRoots();
    return Result::admitted(SealedQuerySnapshot<typename CompleteContextInput::Key, FinalWitness>(
        zc::mv(snapshot), zc::mv(retainedRoots)));
  }
}

template <typename Spec>
DescriptorRegistrationResult QueryDatabase::registerDescriptor() {
  using Binding = QueryDescriptorInventoryBinding<Spec>;
  if constexpr (!requires {
                  Binding::inventoryIdentity;
                  Binding::ordinal;
                  Binding::descriptorType;
                  Binding::role;
                  Binding::ownerPathFamily;
                }) {
    return DescriptorRegistrationResult::rejected(
        DescriptorRegistrationFailure::DescriptorAbsentFromInventory);
  } else {
    using Metadata = zc::RemoveConst<decltype(Spec::descriptor)>;
    static_assert(zc::isSameType<Metadata, InputDescriptorMetadata>() ||
                      zc::isSameType<Metadata, SemanticDescriptorMetadata>() ||
                      zc::isSameType<Metadata, CapabilityDescriptorMetadata>(),
                  "query descriptor must declare one supported literal metadata type");
    if constexpr (zc::isSameType<Metadata, InputDescriptorMetadata>()) {
      static_assert(InputQueryDescriptor<Spec>,
                    "input descriptor must provide exact key and value codecs");
      if constexpr (Binding::role == QueryDescriptorRole::CompleteContextAuthority) {
        static_assert(CompleteContextAuthorityInput<Spec>,
                      "complete-context inventory row requires the exact final-authority verifier");
      }
    } else if constexpr (zc::isSameType<Metadata, SemanticDescriptorMetadata>()) {
      static_assert(SemanticQueryDescriptor<Spec>,
                    "semantic descriptor must provide exact codecs, provider, and verifier");
      static_assert(Binding::role == QueryDescriptorRole::Ordinary,
                    "semantic descriptor cannot own an input authority role");
      static_assert(Spec::descriptor.reuse == ReuseClass::Semantic ||
                        Spec::descriptor.reuse == ReuseClass::Persisted,
                    "semantic descriptor has an illegal reuse class");
      static_assert(Spec::descriptor.equality == QueryEqualityPolicy::CanonicalBytes &&
                        Spec::descriptor.cycle == QueryCyclePolicy::Reject &&
                        Spec::descriptor.cost == QueryCostClass::Linear,
                    "semantic descriptor has an illegal metadata combination");
    } else {
      static_assert(CapabilityQueryDescriptor<Spec>,
                    "capability descriptor must provide exact codec, provider, verifier, "
                    "candidate codec, capability, and failure alternatives");
      static_assert(Binding::role == QueryDescriptorRole::Ordinary,
                    "capability descriptor cannot own an input authority role");
      static_assert(Spec::descriptor.retention == RetentionClass::Retained &&
                        Spec::descriptor.cycle == QueryCyclePolicy::Reject &&
                        Spec::descriptor.cost == QueryCostClass::Linear,
                    "capability descriptor has an illegal metadata combination");
      static_assert(
          _query_detail::CapabilityFailureListShape<typename Spec::FailureAlternatives>::supported,
          "capability descriptor has an unsupported failure-alternative list");
    }

    ErasedKeyValidator keyValidator = [](zc::ArrayPtr<const uint8_t> keyBytes) {
      return Spec::decodeKey(keyBytes) != zc::none;
    };

    zc::Maybe<ErasedProvider> provider;
    zc::Maybe<ErasedVerifier> verifier;
    zc::Maybe<ErasedCapabilityEvaluator> capabilityEvaluator;
    QueryDescriptorInventoryRow descriptor = [&]() {
      if constexpr (zc::isSameType<Metadata, InputDescriptorMetadata>()) {
        return QueryDescriptorInventoryRow{
            Binding::ordinal,
            Binding::descriptorType,
            Spec::descriptor.name,
            Spec::descriptor.domain,
            QueryDescriptorKind::Input,
            Binding::role,
            ReuseClass::Input,
            RetentionClass::Retained,
            Spec::descriptor.durability,
            QueryEqualityPolicy::CanonicalBytes,
            QueryCyclePolicy::Reject,
            QueryCostClass::Linear,
            CapabilityAdmission::AnySnapshot,
            FinalFailureProjection::None,
            Binding::ownerPathFamily,
        };
      } else if constexpr (zc::isSameType<Metadata, SemanticDescriptorMetadata>()) {
        return QueryDescriptorInventoryRow{
            Binding::ordinal,
            Binding::descriptorType,
            Spec::descriptor.name,
            Spec::descriptor.domain,
            QueryDescriptorKind::Semantic,
            Binding::role,
            Spec::descriptor.reuse,
            Spec::descriptor.retention,
            Durability::Frozen,
            Spec::descriptor.equality,
            Spec::descriptor.cycle,
            Spec::descriptor.cost,
            CapabilityAdmission::AnySnapshot,
            FinalFailureProjection::None,
            Binding::ownerPathFamily,
        };
      } else {
        return QueryDescriptorInventoryRow{
            Binding::ordinal,
            Binding::descriptorType,
            Spec::descriptor.name,
            Spec::descriptor.domain,
            QueryDescriptorKind::RevisionLocalCapability,
            Binding::role,
            ReuseClass::RevisionLocal,
            Spec::descriptor.retention,
            Durability::Frozen,
            QueryEqualityPolicy::CanonicalBytes,
            Spec::descriptor.cycle,
            Spec::descriptor.cost,
            Spec::descriptor.admission,
            Spec::descriptor.failureProjection,
            Binding::ownerPathFamily,
        };
      }
    }();

    if constexpr (zc::isSameType<Metadata, SemanticDescriptorMetadata>()) {
      provider = ErasedProvider([](QueryContext& context, zc::ArrayPtr<const uint8_t> keyBytes) {
        auto key = Spec::decodeKey(keyBytes);
        if (key == zc::none) {
          return _query_detail::QueryRequestResultAccess::runtimeRejected(
              QueryRuntimeFailure::InvalidKeyEncoding);
        }
        return _query_detail::encodeResult<Spec>(Spec::provide(context, ZC_REQUIRE_NONNULL(key)));
      });
      verifier = ErasedVerifier(
          [](QueryContext& context, zc::ArrayPtr<const uint8_t> keyBytes, const QueryValue& value) {
            auto key = Spec::decodeKey(keyBytes);
            if (key == zc::none) { return false; }
            auto decoded = _query_detail::decodeValueForVerifier<Spec>(value);
            if (decoded.isRuntimeFailure()) { return false; }
            return Spec::verify(context, ZC_REQUIRE_NONNULL(key), decoded);
          });
    } else if constexpr (zc::isSameType<Metadata, CapabilityDescriptorMetadata>()) {
      capabilityEvaluator = ErasedCapabilityEvaluator([](QueryContext& context,
                                                         zc::ArrayPtr<const uint8_t> keyBytes) {
        auto key = Spec::decodeKey(keyBytes);
        if (key == zc::none) {
          return _query_detail::QueryRequestResultAccess::runtimeRejected(
              QueryRuntimeFailure::InvalidKeyEncoding);
        }
        CapabilityQueryContext<Spec> capabilityContext(context);
        auto candidate = Spec::provide(capabilityContext, ZC_REQUIRE_NONNULL(key));
        if (candidate.isRuntimeRejected()) {
          return _query_detail::QueryRequestResultAccess::runtimeRejected(
              candidate.runtimeFailure());
        }
        if constexpr (Spec::descriptor.admission == CapabilityAdmission::FinalSealedSnapshot) {
          const auto closureKind = capabilityContext.finalSnapshotClosureKind();
          if (closureKind == FinalSnapshotClosureKind::Failure && candidate.isCandidate()) {
            return _query_detail::QueryRequestResultAccess::runtimeRejected(
                QueryRuntimeFailure::InvariantViolation);
          }
        }
        using FailureAlternatives = typename Spec::FailureAlternatives;
        if constexpr (_query_detail::CapabilityFailureListShape<FailureAlternatives>::sourceCount ==
                      1) {
          if (candidate.isSourceRejected()) {
            if constexpr (Spec::descriptor.admission == CapabilityAdmission::FinalSealedSnapshot) {
              const auto projection = Spec::descriptor.failureProjection;
              if (capabilityContext.finalSnapshotClosureKind() ==
                      FinalSnapshotClosureKind::Failure &&
                  projection != FinalFailureProjection::Source &&
                  projection != FinalFailureProjection::SourceOrKey) {
                return _query_detail::QueryRequestResultAccess::runtimeRejected(
                    QueryRuntimeFailure::InvariantViolation);
              }
            }
            using Diagnostic =
                typename _query_detail::CapabilityFailurePayloads<FailureAlternatives>::Source;
            using Alternative = SourceRejection<Diagnostic>;
            if (CapabilityFailureContract<Spec, Alternative>::verify(
                    capabilityContext, ZC_REQUIRE_NONNULL(key), candidate.diagnostics()) !=
                CapabilityRejectionCheck::Verified) {
              return _query_detail::QueryRequestResultAccess::runtimeRejected(
                  QueryRuntimeFailure::VerifierRejected);
            }
            auto payload =
                CapabilityFailureContract<Spec, Alternative>::encode(candidate.diagnostics());
            auto envelope = _query_detail::QueryRequestResultAccess::verifiedCapabilityRejection(
                Spec::descriptor.domain, CapabilityFailureKind::SourceRejected, zc::mv(payload));
            return _query_detail::QueryRequestResultAccess::capabilityRejected(zc::mv(envelope));
          }
        }
        if constexpr (_query_detail::CapabilityFailureListShape<FailureAlternatives>::keyCount ==
                      1) {
          if (candidate.isKeyRejected()) {
            if constexpr (Spec::descriptor.admission == CapabilityAdmission::FinalSealedSnapshot) {
              const auto projection = Spec::descriptor.failureProjection;
              if (capabilityContext.finalSnapshotClosureKind() ==
                      FinalSnapshotClosureKind::Failure &&
                  projection != FinalFailureProjection::Key &&
                  projection != FinalFailureProjection::SourceOrKey) {
                return _query_detail::QueryRequestResultAccess::runtimeRejected(
                    QueryRuntimeFailure::InvariantViolation);
              }
            }
            using KeyFailure =
                typename _query_detail::CapabilityFailurePayloads<FailureAlternatives>::Key;
            using Alternative = KeyRejection<KeyFailure>;
            if (CapabilityFailureContract<Spec, Alternative>::verify(
                    capabilityContext, ZC_REQUIRE_NONNULL(key), candidate.keyFailure()) !=
                CapabilityRejectionCheck::Verified) {
              return _query_detail::QueryRequestResultAccess::runtimeRejected(
                  QueryRuntimeFailure::VerifierRejected);
            }
            auto payload =
                CapabilityFailureContract<Spec, Alternative>::encode(candidate.keyFailure());
            auto envelope = _query_detail::QueryRequestResultAccess::verifiedCapabilityRejection(
                Spec::descriptor.domain, CapabilityFailureKind::KeyRejected, zc::mv(payload));
            return _query_detail::QueryRequestResultAccess::capabilityRejected(zc::mv(envelope));
          }
        }
        if (!candidate.isCandidate()) {
          return _query_detail::QueryRequestResultAccess::runtimeRejected(
              QueryRuntimeFailure::InvariantViolation);
        }
        auto verifiedWitness =
            Spec::verify(capabilityContext, ZC_REQUIRE_NONNULL(key), candidate.candidate());
        if (verifiedWitness == zc::none ||
            ZC_REQUIRE_NONNULL(verifiedWitness).asPtr() != candidate.stableWitness()) {
          return _query_detail::QueryRequestResultAccess::runtimeRejected(
              QueryRuntimeFailure::VerifierRejected);
        }
        ZC_IF_SOME(failure, context.capabilityPublicationFailure()) {
          return _query_detail::QueryRequestResultAccess::runtimeRejected(failure);
        }
        auto stableWitness = zc::heapArray<uint8_t>(candidate.stableWitness());
        auto builder = zc::heap<_query_detail::CapabilityMemoBuilder<Spec>>(
            zc::mv(candidate).takeCandidate(), zc::mv(stableWitness));
        return context.publishCapability(zc::mv(builder));
      });
    }

    return installDescriptor(Binding::inventoryIdentity, descriptor, zc::mv(keyValidator),
                             zc::mv(provider), zc::mv(verifier), zc::mv(capabilityEvaluator));
  }
}

}  // namespace zomlang::compiler::query
