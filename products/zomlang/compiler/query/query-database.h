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
class QueryDatabase;
class QuerySnapshot;

/// \brief Immutable declaration of one closed query kind.
class QueryKindContract final {
public:
  QueryKindContract(QueryKindContract&&) noexcept = default;
  QueryKindContract& operator=(QueryKindContract&&) noexcept = default;
  ZC_DISALLOW_COPY(QueryKindContract);

  /// \brief Constructs an explicit input query contract.
  ZC_NODISCARD static zc::Maybe<QueryKindContract> input(zc::StringPtr domain,
                                                         uint32_t keySchemaVersion,
                                                         uint32_t valueSchemaVersion,
                                                         Durability durability);
  /// \brief Constructs a derived query contract.
  ZC_NODISCARD static zc::Maybe<QueryKindContract> derived(
      zc::StringPtr domain, uint32_t keySchemaVersion, uint32_t valueSchemaVersion,
      ReuseClass reuseClass, RetentionClass retention = RetentionClass::Retained);

  ZC_NODISCARD QueryKindContract clone() const;
  ZC_NODISCARD zc::StringPtr domain() const ZC_LIFETIMEBOUND { return domainField; }
  ZC_NODISCARD uint32_t keySchemaVersion() const noexcept { return keySchemaVersionField; }
  ZC_NODISCARD uint32_t valueSchemaVersion() const noexcept { return valueSchemaVersionField; }
  ZC_NODISCARD ReuseClass reuseClass() const noexcept { return reuseClassField; }
  ZC_NODISCARD RetentionClass retention() const noexcept { return retentionField; }
  ZC_NODISCARD bool isInput() const noexcept { return isInputField; }
  ZC_NODISCARD Durability inputDurability() const noexcept { return inputDurabilityField; }

private:
  QueryKindContract(zc::String&& domain, uint32_t keySchemaVersion, uint32_t valueSchemaVersion,
                    ReuseClass reuseClass, RetentionClass retention, bool isInput,
                    Durability inputDurability) noexcept;

  zc::String domainField;
  uint32_t keySchemaVersionField;
  uint32_t valueSchemaVersionField;
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
  ZC_NODISCARD zc::Vector<QueryRequestResult> getParallelEncoded(
      zc::StringPtr domain, zc::Vector<zc::Array<uint8_t>>&& keyBytes);

  zc::Own<Impl> impl;

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
  using ErasedProvider =
      zc::Function<QueryRequestResult(QueryContext&, zc::ArrayPtr<const uint8_t>)>;
  using ErasedVerifier =
      zc::Function<bool(QueryContext&, zc::ArrayPtr<const uint8_t>, const QueryValue&)>;

  /// \brief Constructs a query database that borrows the session scheduler.
  ///
  /// The scheduler must outlive this database. QueryDatabase never creates or owns worker
  /// threads, so the compiler session remains the sole concurrency-budget authority.
  explicit QueryDatabase(basic::ThreadPool& scheduler);
  ~QueryDatabase() noexcept(false);
  QueryDatabase(QueryDatabase&&) noexcept;
  QueryDatabase& operator=(QueryDatabase&&) noexcept;
  ZC_DISALLOW_COPY(QueryDatabase);

  template <typename Spec>
  ZC_NODISCARD zc::Maybe<QueryKindId> registerInputKind();

  template <typename Spec>
  ZC_NODISCARD zc::Maybe<QueryKindId> registerDerivedKind();

  ZC_NODISCARD QuerySnapshot snapshot();
  ZC_NODISCARD zc::Maybe<InputTransaction> beginInputTransaction();

private:
  struct Impl;
  ZC_NODISCARD zc::Maybe<QueryKindId> installInput(QueryKindContract&& contract);
  ZC_NODISCARD zc::Maybe<QueryKindId> installDerived(QueryKindContract&& contract,
                                                     ErasedProvider&& provider,
                                                     ErasedVerifier&& verifier);

  zc::Own<Impl> impl;

  friend class QueryContext;
  friend class QuerySnapshot;
  friend class InputTransaction;
};

namespace _query_detail {

template <typename Spec>
TypedQueryResult<typename Spec::Value> decodeResult(QueryRequestResult&& result) {
  using Value = typename Spec::Value;
  if (!result.isCompleted()) { return TypedQueryResult<Value>::runtimeFailure(result.failure()); }
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
TypedQueryResult<typename Spec::Value> QueryContext::get(const typename Spec::Key& key) {
  return _query_detail::decodeResult<Spec>(getEncoded(Spec::domain(), Spec::encodeKey(key)));
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
  return installInput(Spec::contract());
}

template <typename Spec>
zc::Maybe<QueryKindId> QueryDatabase::registerDerivedKind() {
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
  return installDerived(Spec::contract(), zc::mv(provider), zc::mv(verifier));
}

}  // namespace zomlang::compiler::query
