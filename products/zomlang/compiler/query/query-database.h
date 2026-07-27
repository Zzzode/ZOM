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
class CapabilityQueryContext;

template <typename Key>
struct ActiveMaterialization;

template <typename Key>
struct ActiveMembership;

template <typename Spec>
struct ActiveMaterializerPermission {
  static constexpr bool allowed = false;
};
class QueryDatabase;
class QuerySnapshot;

namespace _query_detail {
class CapabilityMemoBuilderBase;
}

/// \brief Immutable declaration of one closed query kind.
class QueryKindContract final {
public:
  QueryKindContract(QueryKindContract&&) noexcept = default;
  QueryKindContract& operator=(QueryKindContract&&) noexcept = default;
  ZC_DISALLOW_COPY(QueryKindContract);

  /// \brief Constructs an explicit input query contract.
  ZC_NODISCARD static zc::Maybe<QueryKindContract> input(zc::StringPtr domain,
                                                         Durability durability);
  /// \brief Constructs a derived query contract.
  ZC_NODISCARD static zc::Maybe<QueryKindContract> derived(
      zc::StringPtr domain, ReuseClass reuseClass,
      RetentionClass retention = RetentionClass::Retained);

  ZC_NODISCARD QueryKindContract clone() const;
  ZC_NODISCARD zc::StringPtr domain() const ZC_LIFETIMEBOUND { return domainField; }
  ZC_NODISCARD ReuseClass reuseClass() const noexcept { return reuseClassField; }
  ZC_NODISCARD RetentionClass retention() const noexcept { return retentionField; }
  ZC_NODISCARD bool isInput() const noexcept { return isInputField; }
  ZC_NODISCARD Durability inputDurability() const noexcept { return inputDurabilityField; }

private:
  QueryKindContract(zc::String&& domain, ReuseClass reuseClass, RetentionClass retention,
                    bool isInput, Durability inputDurability) noexcept;

  zc::String domainField;
  ReuseClass reuseClassField;
  RetentionClass retentionField;
  bool isInputField;
  Durability inputDurabilityField;
};

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

/// \brief Provider result for one move-only revision-local capability candidate.
template <typename Capability>
class CapabilityProviderResult final {
public:
  CapabilityProviderResult(CapabilityProviderResult&&) noexcept = default;
  CapabilityProviderResult& operator=(CapabilityProviderResult&&) noexcept = default;
  ZC_DISALLOW_COPY(CapabilityProviderResult);

  ZC_NODISCARD static CapabilityProviderResult value(zc::Own<Capability>&& candidate,
                                                     zc::Array<uint8_t>&& stableWitness) {
    ZC_IREQUIRE(candidate.get() != nullptr, "capability provider returned no candidate");
    zc::Maybe<zc::Own<Capability>> retained(zc::mv(candidate));
    return CapabilityProviderResult(QueryValueKind::Value, zc::mv(retained), zc::mv(stableWitness),
                                    nullptr, zc::none);
  }
  ZC_NODISCARD static CapabilityProviderResult absence() {
    zc::Maybe<zc::Own<Capability>> noCandidate;
    return CapabilityProviderResult(QueryValueKind::Absence, zc::mv(noCandidate), nullptr, nullptr,
                                    zc::none);
  }
  ZC_NODISCARD static CapabilityProviderResult semanticFailure(
      zc::Array<uint8_t>&& canonicalBytes) {
    zc::Maybe<zc::Own<Capability>> noCandidate;
    return CapabilityProviderResult(QueryValueKind::SemanticFailure, zc::mv(noCandidate), nullptr,
                                    zc::mv(canonicalBytes), zc::none);
  }
  ZC_NODISCARD static CapabilityProviderResult runtimeFailure(QueryRuntimeFailure failure) {
    zc::Maybe<zc::Own<Capability>> noCandidate;
    zc::Maybe<QueryRuntimeFailure> retainedFailure(failure);
    return CapabilityProviderResult(QueryValueKind::Absence, zc::mv(noCandidate), nullptr, nullptr,
                                    zc::mv(retainedFailure));
  }

  ZC_NODISCARD bool isRuntimeFailure() const noexcept { return runtimeFailureField != zc::none; }
  ZC_NODISCARD QueryRuntimeFailure runtimeFailure() const {
    return ZC_REQUIRE_NONNULL(runtimeFailureField);
  }
  ZC_NODISCARD QueryValueKind kind() const noexcept { return kindField; }
  ZC_NODISCARD const Capability& candidate() const ZC_LIFETIMEBOUND {
    return *ZC_REQUIRE_NONNULL(candidateField);
  }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> stableWitness() const ZC_LIFETIMEBOUND {
    return stableWitnessField.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> semanticFailureBytes() const ZC_LIFETIMEBOUND {
    return semanticFailureBytesField.asPtr();
  }
  ZC_NODISCARD zc::Own<Capability> takeCandidate() && {
    return zc::mv(ZC_REQUIRE_NONNULL(candidateField));
  }

private:
  CapabilityProviderResult(QueryValueKind kind, zc::Maybe<zc::Own<Capability>>&& candidate,
                           zc::Array<uint8_t>&& stableWitness,
                           zc::Array<uint8_t>&& semanticFailureBytes,
                           zc::Maybe<QueryRuntimeFailure>&& runtimeFailure) noexcept
      : kindField(kind),
        candidateField(zc::mv(candidate)),
        stableWitnessField(zc::mv(stableWitness)),
        semanticFailureBytesField(zc::mv(semanticFailureBytes)),
        runtimeFailureField(zc::mv(runtimeFailure)) {}

  QueryValueKind kindField;
  zc::Maybe<zc::Own<Capability>> candidateField;
  zc::Array<uint8_t> stableWitnessField;
  zc::Array<uint8_t> semanticFailureBytesField;
  zc::Maybe<QueryRuntimeFailure> runtimeFailureField;
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
  ZC_NODISCARD TypedQueryResult<QueryCapabilityLease<const typename Spec::Capability>>
  getCapability(const typename Spec::Key& key);

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
  ZC_NODISCARD zc::Maybe<QueryRuntimeFailure> capabilityPublicationFailure() const;
  ZC_NODISCARD bool activeMaterializationReady() const;
  ZC_NODISCARD const SemanticContextCapabilityResources& semanticContextResources() const
      ZC_LIFETIMEBOUND;

  zc::Own<Impl> impl;

  friend class QueryDatabase;
  friend class CapabilityQueryContext;
};

/// \brief Provider-only tracked read and materialization authority for capability queries.
///
/// Canonical Semantic and Persisted providers receive QueryContext and therefore cannot access
/// process-local semantic resources. Only RevisionLocal capability providers receive this type.
class CapabilityQueryContext final {
public:
  ZC_DISALLOW_COPY_AND_MOVE(CapabilityQueryContext);

  ZC_NODISCARD bool isCancelled() const { return context.isCancelled(); }

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> get(const typename Spec::Key& key) {
    return context.get<Spec>(key);
  }

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<QueryCapabilityLease<const typename Spec::Capability>>
  getCapability(const typename Spec::Key& key) {
    return context.getCapability<Spec>(key);
  }

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<typename Spec::Value> probeInput(const typename Spec::Key& key) {
    return context.probeInput<Spec>(key);
  }

  /// \brief Demands membership and materializes one active key for this registered descriptor.
  template <typename Key, typename... Authority>
  ZC_NODISCARD TypedQueryResult<typename ActiveMaterialization<Key>::Handle> materializeActive(
      const Key& key, const Authority&... authority) {
    if (!activeMaterializationAllowed || !context.activeMaterializationReady()) {
      return TypedQueryResult<typename ActiveMaterialization<Key>::Handle>::runtimeFailure(
          QueryRuntimeFailure::ProviderRejected);
    }
    auto membership = ActiveMembership<Key>::demand(context, key, authority...);
    if (membership.isRuntimeFailure()) {
      return TypedQueryResult<typename ActiveMaterialization<Key>::Handle>::runtimeFailure(
          membership.runtimeFailure());
    }
    if (membership.kind() != QueryValueKind::Value || !membership.value()) {
      return TypedQueryResult<typename ActiveMaterialization<Key>::Handle>::absence();
    }
    return ActiveMaterialization<Key>::materialize(context.semanticContextResources(), key);
  }

private:
  explicit CapabilityQueryContext(QueryContext& context, bool activeMaterializationAllowed) noexcept
      : context(context), activeMaterializationAllowed(activeMaterializationAllowed) {}
  QueryContext& context;
  bool activeMaterializationAllowed;

  friend class QueryDatabase;
};

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
  ZC_NODISCARD TypedQueryResult<QueryCapabilityLease<const typename Spec::Capability>>
  getCapability(const typename Spec::Key& key, const CancellationSource::Token& cancellation) const;

  template <typename Spec>
  ZC_NODISCARD TypedQueryResult<QueryCapabilityLease<const typename Spec::Capability>>
  getCapability(const typename Spec::Key& key) const;

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
};

/// \brief Exclusive private builder for the next complete explicit-input root.
class InputTransaction final {
public:
  InputTransaction(InputTransaction&&) noexcept;
  InputTransaction& operator=(InputTransaction&&) noexcept;
  ~InputTransaction() noexcept(false);
  ZC_DISALLOW_COPY(InputTransaction);

  template <typename Spec>
  bool set(const typename Spec::Key& key, const typename Spec::Value& value);

  /// \brief Removes one staged mutable input from the next complete input root.
  template <typename Spec>
  bool erase(const typename Spec::Key& key);

  ZC_NODISCARD zc::Maybe<DatabaseRevision> commit();
  void abandon();

private:
  struct Impl;
  explicit InputTransaction(zc::Own<Impl>&& impl) noexcept;
  bool stageEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes, QueryValue&& value);
  bool eraseEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes);

  zc::Own<Impl> impl;

  friend class QueryDatabase;
};

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
  explicit QueryDatabase(basic::ThreadPool& scheduler);
  QueryDatabase(basic::ThreadPool& scheduler,
                zc::Arc<SemanticContextCapabilityArena>&& capabilityArena);
  ~QueryDatabase() noexcept(false);
  QueryDatabase(QueryDatabase&&) noexcept;
  QueryDatabase& operator=(QueryDatabase&&) noexcept;
  ZC_DISALLOW_COPY(QueryDatabase);

  template <typename Spec>
  ZC_NODISCARD zc::Maybe<QueryKindId> registerInputKind();

  template <typename Spec>
  ZC_NODISCARD zc::Maybe<QueryKindId> registerDerivedKind();

  template <typename Spec>
  ZC_NODISCARD zc::Maybe<QueryKindId> registerRevisionLocalCapabilityKind();

  ZC_NODISCARD QuerySnapshot snapshot();
  ZC_NODISCARD zc::Maybe<InputTransaction> beginInputTransaction();
  /// \brief Permanently closes the explicit-input root after the final session transaction.
  ///
  /// Sealing succeeds exactly once and only when no transaction is open. Every later
  /// beginInputTransaction() call fails while existing snapshots remain readable.
  ZC_NODISCARD bool sealInputRoot();

private:
  struct Impl;
  ZC_NODISCARD zc::Maybe<QueryKindId> installInput(QueryKindContract&& contract,
                                                   ErasedKeyValidator&& keyValidator);
  ZC_NODISCARD zc::Maybe<QueryKindId> installDerived(QueryKindContract&& contract,
                                                     ErasedKeyValidator&& keyValidator,
                                                     ErasedProvider&& provider,
                                                     ErasedVerifier&& verifier);
  ZC_NODISCARD zc::Maybe<QueryKindId> installCapability(QueryKindContract&& contract,
                                                        ErasedKeyValidator&& keyValidator,
                                                        ErasedCapabilityEvaluator&& evaluator);

  zc::Own<Impl> impl;

  friend class QueryContext;
  friend class QuerySnapshot;
  friend class InputTransaction;
};

namespace _query_detail {

class CapabilityMemoBuilderBase {
public:
  virtual ~CapabilityMemoBuilderBase() noexcept(false) = default;
  ZC_DISALLOW_COPY_AND_MOVE(CapabilityMemoBuilderBase);

  virtual zc::Arc<RevisionLocalCapabilityMemoBase> publish(
      CanonicalQueryKey&& key, DatabaseRevision revision, zc::Arc<SnapshotCapabilityArena>&& arena,
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies) = 0;

protected:
  CapabilityMemoBuilderBase() = default;
};

template <typename Capability>
class CapabilityMemoBuilder final : public CapabilityMemoBuilderBase {
public:
  CapabilityMemoBuilder(zc::Own<Capability>&& candidate, zc::Array<uint8_t>&& stableWitness)
      : candidateField(zc::mv(candidate)), stableWitnessField(zc::mv(stableWitness)) {}

  zc::Arc<RevisionLocalCapabilityMemoBase> publish(
      CanonicalQueryKey&& key, DatabaseRevision revision, zc::Arc<SnapshotCapabilityArena>&& arena,
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies) override {
    return zc::arc<RevisionLocalCapabilityMemo<Capability>>(
        zc::mv(key), revision, zc::mv(arena), zc::mv(retainedDependencies),
        zc::mv(stableWitnessField), zc::mv(candidateField));
  }

private:
  zc::Own<Capability> candidateField;
  zc::Array<uint8_t> stableWitnessField;
};

template <typename Spec>
TypedQueryResult<typename Spec::Value> decodeResult(QueryRequestResult&& result) {
  using Value = typename Spec::Value;
  if (!result.isCompleted()) { return TypedQueryResult<Value>::runtimeFailure(result.failure()); }
  if (result.isCapability()) {
    return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  const auto& erased = result.value();
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
  if (result.isRuntimeFailure()) { return QueryRequestResult::failed(result.runtimeFailure()); }
  switch (result.kind()) {
    case QueryValueKind::Value:
      return QueryRequestResult::completed(QueryValue::value(Spec::encodeValue(result.value())));
    case QueryValueKind::Absence:
      return QueryRequestResult::completed(QueryValue::absence());
    case QueryValueKind::SemanticFailure:
      return QueryRequestResult::completed(
          QueryValue::semanticFailure(zc::heapArray<uint8_t>(result.semanticFailureBytes())));
  }
  return QueryRequestResult::failed(QueryRuntimeFailure::InvariantViolation);
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

}  // namespace _query_detail

template <typename Spec>
class CapabilityResultDecoder final {
public:
  using Capability = typename Spec::Capability;
  using Lease = QueryCapabilityLease<const Capability>;

  ZC_NODISCARD static TypedQueryResult<Lease> decode(QueryRequestResult&& result) {
    if (!result.isCompleted()) { return TypedQueryResult<Lease>::runtimeFailure(result.failure()); }
    if (result.isCapability()) {
      const auto expected = _query_detail::capabilityTypeIdentity<Capability>();
      if (result.capabilityMemo().capabilityTypeIdentity() != expected) {
        return TypedQueryResult<Lease>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
      }
      auto erased = result.takeCapabilityMemo();
      auto typed = erased.template downcast<RevisionLocalCapabilityMemo<Capability>>();
      return TypedQueryResult<Lease>::value(Lease(zc::mv(typed)));
    }
    switch (result.value().kind()) {
      case QueryValueKind::Value:
        return TypedQueryResult<Lease>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
      case QueryValueKind::Absence:
        return TypedQueryResult<Lease>::absence();
      case QueryValueKind::SemanticFailure:
        return TypedQueryResult<Lease>::semanticFailure(
            zc::heapArray<uint8_t>(result.value().canonicalBytes()));
    }
    return TypedQueryResult<Lease>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
};

template <typename Spec>
TypedQueryResult<typename Spec::Value> QueryContext::get(const typename Spec::Key& key) {
  return _query_detail::decodeResult<Spec>(getEncoded(Spec::domain(), Spec::encodeKey(key)));
}

template <typename Spec>
TypedQueryResult<QueryCapabilityLease<const typename Spec::Capability>> QueryContext::getCapability(
    const typename Spec::Key& key) {
  return CapabilityResultDecoder<Spec>::decode(getEncoded(Spec::domain(), Spec::encodeKey(key)));
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QueryContext::probeInput(const typename Spec::Key& key) {
  return _query_detail::decodeResult<Spec>(probeInputEncoded(Spec::domain(), Spec::encodeKey(key)));
}

template <typename Spec>
zc::Vector<TypedQueryResult<typename Spec::Value>> QueryContext::getParallel(
    zc::ArrayPtr<const typename Spec::Key> keys) {
  zc::Vector<zc::Array<uint8_t>> encoded;
  for (const auto& key : keys) { encoded.add(Spec::encodeKey(key)); }
  auto erased = getParallelEncoded(Spec::domain(), zc::mv(encoded));
  zc::Vector<TypedQueryResult<typename Spec::Value>> result;
  for (auto& item : erased) { result.add(_query_detail::decodeResult<Spec>(zc::mv(item))); }
  return result;
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QuerySnapshot::get(
    const typename Spec::Key& key, const CancellationSource::Token& cancellation) const {
  return _query_detail::decodeResult<Spec>(
      getEncoded(Spec::domain(), Spec::encodeKey(key), cancellation));
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QuerySnapshot::get(const typename Spec::Key& key) const {
  CancellationSource cancellation;
  return get<Spec>(key, cancellation.token());
}

template <typename Spec>
TypedQueryResult<QueryCapabilityLease<const typename Spec::Capability>>
QuerySnapshot::getCapability(const typename Spec::Key& key,
                             const CancellationSource::Token& cancellation) const {
  return CapabilityResultDecoder<Spec>::decode(
      getEncoded(Spec::domain(), Spec::encodeKey(key), cancellation));
}

template <typename Spec>
TypedQueryResult<QueryCapabilityLease<const typename Spec::Capability>>
QuerySnapshot::getCapability(const typename Spec::Key& key) const {
  CancellationSource cancellation;
  return getCapability<Spec>(key, cancellation.token());
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QuerySnapshot::probeInput(
    const typename Spec::Key& key, const CancellationSource::Token& cancellation) const {
  return _query_detail::decodeResult<Spec>(
      probeInputEncoded(Spec::domain(), Spec::encodeKey(key), cancellation));
}

template <typename Spec>
TypedQueryResult<typename Spec::Value> QuerySnapshot::probeInput(
    const typename Spec::Key& key) const {
  CancellationSource cancellation;
  return probeInput<Spec>(key, cancellation.token());
}

template <typename Spec>
zc::Maybe<MemoMetadata> QuerySnapshot::metadata(const typename Spec::Key& key) const {
  return metadataEncoded(Spec::domain(), Spec::encodeKey(key));
}

template <typename Spec>
zc::Vector<DependencyGroup> QuerySnapshot::dependencies(const typename Spec::Key& key) const {
  return dependenciesEncoded(Spec::domain(), Spec::encodeKey(key));
}

template <typename Spec>
bool QuerySnapshot::evictValue(const typename Spec::Key& key) const {
  return evictValueEncoded(Spec::domain(), Spec::encodeKey(key));
}

template <typename Spec>
bool QuerySnapshot::hasRetainedValue(const typename Spec::Key& key) const {
  return hasRetainedValueEncoded(Spec::domain(), Spec::encodeKey(key));
}

template <typename Spec>
zc::Maybe<QueryKeyFingerprint> QuerySnapshot::keyFingerprint(const typename Spec::Key& key) const {
  return keyFingerprintEncoded(Spec::domain(), Spec::encodeKey(key));
}

template <typename Spec>
bool InputTransaction::set(const typename Spec::Key& key, const typename Spec::Value& value) {
  return stageEncoded(Spec::domain(), Spec::encodeKey(key),
                      QueryValue::value(Spec::encodeValue(value)));
}

template <typename Spec>
bool InputTransaction::erase(const typename Spec::Key& key) {
  return eraseEncoded(Spec::domain(), Spec::encodeKey(key));
}

template <typename Spec>
zc::Maybe<QueryKindId> QueryDatabase::registerInputKind() {
  ErasedKeyValidator keyValidator = [](zc::ArrayPtr<const uint8_t> keyBytes) {
    return Spec::decodeKey(keyBytes) != zc::none;
  };
  return installInput(Spec::contract(), zc::mv(keyValidator));
}

template <typename Spec>
zc::Maybe<QueryKindId> QueryDatabase::registerDerivedKind() {
  ErasedKeyValidator keyValidator = [](zc::ArrayPtr<const uint8_t> keyBytes) {
    return Spec::decodeKey(keyBytes) != zc::none;
  };
  ErasedProvider provider = [](QueryContext& context, zc::ArrayPtr<const uint8_t> keyBytes) {
    auto key = Spec::decodeKey(keyBytes);
    if (key == zc::none) {
      return QueryRequestResult::failed(QueryRuntimeFailure::InvalidKeyEncoding);
    }
    return _query_detail::encodeResult<Spec>(Spec::provide(context, ZC_REQUIRE_NONNULL(key)));
  };
  ErasedVerifier verifier = [](QueryContext& context, zc::ArrayPtr<const uint8_t> keyBytes,
                               const QueryValue& value) {
    auto key = Spec::decodeKey(keyBytes);
    if (key == zc::none) { return false; }
    auto decoded = _query_detail::decodeValueForVerifier<Spec>(value);
    if (decoded.isRuntimeFailure()) { return false; }
    return Spec::verify(context, ZC_REQUIRE_NONNULL(key), decoded);
  };
  return installDerived(Spec::contract(), zc::mv(keyValidator), zc::mv(provider), zc::mv(verifier));
}

template <typename Spec>
zc::Maybe<QueryKindId> QueryDatabase::registerRevisionLocalCapabilityKind() {
  using Capability = typename Spec::Capability;
  ErasedKeyValidator keyValidator = [](zc::ArrayPtr<const uint8_t> keyBytes) {
    return Spec::decodeKey(keyBytes) != zc::none;
  };
  ErasedCapabilityEvaluator evaluator = [](QueryContext& context,
                                           zc::ArrayPtr<const uint8_t> keyBytes) {
    auto key = Spec::decodeKey(keyBytes);
    if (key == zc::none) {
      return QueryRequestResult::failed(QueryRuntimeFailure::InvalidKeyEncoding);
    }
    CapabilityQueryContext capabilityContext(context, ActiveMaterializerPermission<Spec>::allowed);
    auto candidate = Spec::provide(capabilityContext, ZC_REQUIRE_NONNULL(key));
    if (candidate.isRuntimeFailure()) {
      return QueryRequestResult::failed(candidate.runtimeFailure());
    }
    switch (candidate.kind()) {
      case QueryValueKind::Absence:
        return QueryRequestResult::completed(QueryValue::absence());
      case QueryValueKind::SemanticFailure:
        return QueryRequestResult::completed(
            QueryValue::semanticFailure(zc::heapArray<uint8_t>(candidate.semanticFailureBytes())));
      case QueryValueKind::Value:
        break;
    }
    auto verifiedWitness =
        Spec::verify(capabilityContext, ZC_REQUIRE_NONNULL(key), candidate.candidate());
    if (verifiedWitness == zc::none ||
        ZC_REQUIRE_NONNULL(verifiedWitness).asPtr() != candidate.stableWitness()) {
      return QueryRequestResult::failed(QueryRuntimeFailure::VerifierRejected);
    }
    ZC_IF_SOME(failure, context.capabilityPublicationFailure()) {
      return QueryRequestResult::failed(failure);
    }
    auto stableWitness = zc::heapArray<uint8_t>(candidate.stableWitness());
    auto builder = zc::heap<_query_detail::CapabilityMemoBuilder<Capability>>(
        zc::mv(candidate).takeCandidate(), zc::mv(stableWitness));
    return context.publishCapability(zc::mv(builder));
  };
  return installCapability(Spec::contract(), zc::mv(keyValidator), zc::mv(evaluator));
}

}  // namespace zomlang::compiler::query
