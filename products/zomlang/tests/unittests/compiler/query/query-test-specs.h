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

#include <unistd.h>

#include "zc/core/debug.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::module_graph_query {

/// \brief Test-only definition for the accepted future production inventory role.
struct CompleteCompilationContextAuthorityInput final {
  using Key = uint32_t;
  using Value = uint32_t;

  static constexpr query::InputDescriptorMetadata descriptor{
      "CompleteCompilationContextAuthorityInput"_zcc,
      "zom.input.complete-compilation-context-authority"_zcc, query::Durability::Frozen};

  static zc::Array<uint8_t> encodeKey(const Key& key) {
    auto bytes = zc::heapArray<uint8_t>(4);
    bytes[0] = static_cast<uint8_t>(key >> 24);
    bytes[1] = static_cast<uint8_t>(key >> 16);
    bytes[2] = static_cast<uint8_t>(key >> 8);
    bytes[3] = static_cast<uint8_t>(key);
    return bytes;
  }

  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
    if (bytes.size() != 4) { return zc::none; }
    return (uint32_t{bytes[0]} << 24) | (uint32_t{bytes[1]} << 16) | (uint32_t{bytes[2]} << 8) |
           uint32_t{bytes[3]};
  }

  static zc::Array<uint8_t> encodeValue(const Value& value) { return encodeKey(value); }
  static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
    return decodeKey(bytes);
  }

  static query::FinalAuthorityCheck verifyFinalAuthority(const query::QuerySnapshot&,
                                                         const Key& key, const Value& value,
                                                         const identity::Sha256Digest& witness) {
    if (key != value) { return query::FinalAuthorityCheck::Rejected; }
    for (uint8_t byte : witness.bytes()) {
      if (byte != static_cast<uint8_t>(key)) { return query::FinalAuthorityCheck::Rejected; }
    }
    return query::FinalAuthorityCheck::Verified;
  }
};

}  // namespace zomlang::compiler::driver::module_graph_query

namespace zomlang::compiler::query::test {

inline basic::ThreadPool& queryTestScheduler() {
  static basic::ThreadPool scheduler(4);
  return scheduler;
}

inline QueryDatabase queryTestDatabase() {
  return QueryDatabase(queryTestScheduler(), queryTestDescriptorInventory());
}

inline zc::Array<uint8_t> encodeUint32(uint32_t value) {
  auto bytes = zc::heapArray<uint8_t>(4);
  bytes[0] = static_cast<uint8_t>(value >> 24);
  bytes[1] = static_cast<uint8_t>(value >> 16);
  bytes[2] = static_cast<uint8_t>(value >> 8);
  bytes[3] = static_cast<uint8_t>(value);
  return bytes;
}

inline zc::Maybe<uint32_t> decodeUint32(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() != 4) { return zc::none; }
  return (uint32_t{bytes[0]} << 24) | (uint32_t{bytes[1]} << 16) | (uint32_t{bytes[2]} << 8) |
         uint32_t{bytes[3]};
}

struct LowInput {
  using Key = uint32_t;
  using Value = uint32_t;
  static constexpr InputDescriptorMetadata descriptor{"LowInput"_zcc, "test.input.low"_zcc,
                                                      Durability::Low};
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }
  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) { return decodeUint32(bytes); }
  static zc::Array<uint8_t> encodeValue(const Value& value) { return encodeUint32(value); }
  static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
    return decodeUint32(bytes);
  }
};

struct MediumInput final : LowInput {
  static constexpr InputDescriptorMetadata descriptor{"MediumInput"_zcc, "test.input.medium"_zcc,
                                                      Durability::Medium};
};

struct HighInput final : LowInput {
  static constexpr InputDescriptorMetadata descriptor{"HighInput"_zcc, "test.input.high"_zcc,
                                                      Durability::High};
};

struct FrozenInput final : LowInput {
  static constexpr InputDescriptorMetadata descriptor{"FrozenInput"_zcc, "test.input.frozen"_zcc,
                                                      Durability::Frozen};
};

struct MalformedLowInputKey final : LowInput {
  static constexpr InputDescriptorMetadata descriptor{
      "MalformedLowInputKey"_zcc, "test.input.malformed-low-key"_zcc, Durability::Low};
  static zc::Array<uint8_t> encodeKey(const Key&) { return zc::heapArray<uint8_t>(3); }
};

constexpr uint32_t CAPABILITY_GENERATION_INPUT_MASK = uint32_t{1} << 31;

inline uint32_t capabilityGenerationInputKey(uint32_t key) {
  return key ^ CAPABILITY_GENERATION_INPUT_MASK;
}

class LeafCapability final {
public:
  LeafCapability(uint32_t value, uint32_t generation) noexcept
      : valueField(value), generationField(generation) {}

  ZC_NODISCARD uint32_t value() const noexcept { return valueField; }
  ZC_NODISCARD uint32_t generation() const noexcept { return generationField; }

private:
  uint32_t valueField;
  uint32_t generationField;
};

class ParentCapability final {
public:
  ParentCapability(uint32_t value, uint32_t generation) noexcept
      : valueField(value), generationField(generation) {}

  ZC_NODISCARD uint32_t value() const noexcept { return valueField; }
  ZC_NODISCARD uint32_t generation() const noexcept { return generationField; }

private:
  uint32_t valueField;
  uint32_t generationField;
};

#define ZOM_DECLARE_TEST_CAPABILITY_QUERY(Name, CapabilityType, NameLiteral, DomainLiteral,   \
                                          Admission)                                          \
  struct Name final {                                                                         \
    using Key = uint32_t;                                                                     \
    using Capability = CapabilityType;                                                        \
    using FailureAlternatives = CapabilityFailureList<>;                                      \
                                                                                              \
    static constexpr CapabilityDescriptorMetadata descriptor{NameLiteral,                     \
                                                             DomainLiteral,                   \
                                                             RetentionClass::Retained,        \
                                                             QueryCyclePolicy::Reject,        \
                                                             QueryCostClass::Linear,          \
                                                             CapabilityAdmission::Admission}; \
    static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }         \
    static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) {                      \
      return decodeUint32(bytes);                                                             \
    }                                                                                         \
    static CapabilityProviderResult<Name> provide(CapabilityQueryContext<Name>& context,      \
                                                  const Key& key);                            \
    static zc::Maybe<zc::Array<uint8_t>> verify(CapabilityQueryContext<Name>&, const Key&,    \
                                                const Capability& candidate);                 \
  }

ZOM_DECLARE_TEST_CAPABILITY_QUERY(LeafCapabilityQuery, LeafCapability, "LeafCapabilityQuery"_zcc,
                                  "test.capability.leaf"_zcc, AnySnapshot);
ZOM_DECLARE_TEST_CAPABILITY_QUERY(ParentCapabilityQuery, ParentCapability,
                                  "ParentCapabilityQuery"_zcc, "test.capability.parent"_zcc,
                                  AnySnapshot);
ZOM_DECLARE_TEST_CAPABILITY_QUERY(SlowCapabilityQuery, LeafCapability, "SlowCapabilityQuery"_zcc,
                                  "test.capability.slow"_zcc, AnySnapshot);
ZOM_DECLARE_TEST_CAPABILITY_QUERY(RejectedCapabilityQuery, LeafCapability,
                                  "RejectedCapabilityQuery"_zcc, "test.capability.rejected"_zcc,
                                  AnySnapshot);
ZOM_DECLARE_TEST_CAPABILITY_QUERY(FinalSealedCapabilityQuery, LeafCapability,
                                  "FinalSealedCapabilityQuery"_zcc,
                                  "test.capability.final-sealed"_zcc, FinalSealedSnapshot);
ZOM_DECLARE_TEST_CAPABILITY_QUERY(FinalSealedParentCapabilityQuery, ParentCapability,
                                  "FinalSealedParentCapabilityQuery"_zcc,
                                  "test.capability.final-sealed-parent"_zcc, FinalSealedSnapshot);

#undef ZOM_DECLARE_TEST_CAPABILITY_QUERY

struct TerminalCapabilityQuery final {
  using Key = uint32_t;
  using Capability = LeafCapability;
  using FailureAlternatives = CapabilityFailureList<KeyRejection<uint32_t>>;

  static constexpr CapabilityDescriptorMetadata descriptor{
      "TerminalCapabilityQuery"_zcc, "test.capability.terminal"_zcc,
      RetentionClass::Retained,      QueryCyclePolicy::Reject,
      QueryCostClass::Linear,        CapabilityAdmission::AnySnapshot};
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }
  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) { return decodeUint32(bytes); }
  static CapabilityProviderResult<TerminalCapabilityQuery> provide(
      CapabilityQueryContext<TerminalCapabilityQuery>&, const Key& key);
  static zc::Maybe<zc::Array<uint8_t>> verify(CapabilityQueryContext<TerminalCapabilityQuery>&,
                                              const Key&, const Capability& candidate);
};

struct CapabilityRejectionProjectionQuery final {
  using Key = uint32_t;
  using Value = uint32_t;

  static constexpr SemanticDescriptorMetadata descriptor{
      "CapabilityRejectionProjectionQuery"_zcc,
      "test.query.capability-rejection-projection"_zcc,
      ReuseClass::Semantic,
      RetentionClass::Retained,
      QueryEqualityPolicy::CanonicalBytes,
      QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }
  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) { return decodeUint32(bytes); }
  static zc::Array<uint8_t> encodeValue(const Value& value) { return encodeUint32(value); }
  static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
    return decodeUint32(bytes);
  }
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key);
  static bool verify(QueryContext& context, const Key& key, const TypedQueryResult<Value>& result);
};

struct AddTenQuery {
  using Key = uint32_t;
  using Value = uint32_t;
  static constexpr SemanticDescriptorMetadata descriptor{"AddTenQuery"_zcc,
                                                         "test.query.add-ten"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }
  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) { return decodeUint32(bytes); }
  static zc::Array<uint8_t> encodeValue(const Value& value) { return encodeUint32(value); }
  static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
    return decodeUint32(bytes);
  }
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto input = context.get<LowInput>(key);
    if (input.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(input.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(input.value() + 10);
  }
  static bool verify(QueryContext&, const Key&, const TypedQueryResult<Value>& result) {
    return result.kind() == QueryValueKind::Value;
  }
};

struct ParityProjectionQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"ParityProjectionQuery"_zcc,
                                                         "test.query.parity"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto source = context.get<AddTenQuery>(key);
    if (source.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(source.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(source.value() & 1U);
  }
};

struct ChangedProjectionQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "ChangedProjectionQuery"_zcc, "test.query.changed-projection"_zcc, ReuseClass::Semantic,
      RetentionClass::Retained,     QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
};

struct EvictableQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "EvictableQuery"_zcc,      "test.query.evictable"_zcc,          ReuseClass::Semantic,
      RetentionClass::Evictable, QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
};

struct BranchQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"BranchQuery"_zcc,
                                                         "test.query.branch"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key&) {
    auto selector = context.get<LowInput>(0);
    if (selector.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(selector.runtimeFailure());
    }
    auto selected = context.get<LowInput>(selector.value() == 0 ? 1 : 2);
    if (selected.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(selected.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(selected.value());
  }
};

struct ParallelSumQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "ParallelSumQuery"_zcc,   "test.query.parallel-sum"_zcc,       ReuseClass::Semantic,
      RetentionClass::Retained, QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    uint32_t keys[] = {key + 1, key};
    auto values = context.getParallel<LowInput>(zc::arrayPtr(keys));
    if (values.size() != 2 || values[0].isRuntimeFailure() || values[1].isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::ProviderRejected);
    }
    return TypedQueryResult<Value>::value(values[0].value() + values[1].value());
  }
};

struct ParallelPositionalQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "ParallelPositionalQuery"_zcc, "test.query.parallel-positional"_zcc, ReuseClass::Semantic,
      RetentionClass::Retained,      QueryEqualityPolicy::CanonicalBytes,  QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    uint32_t keys[] = {key + 3, key + 1, key + 2, key};
    auto values = context.getParallel<LowInput>(zc::arrayPtr(keys));
    if (values.size() != 4) {
      return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::ProviderRejected);
    }
    for (const auto& value : values) {
      if (value.isRuntimeFailure() || value.kind() != QueryValueKind::Value) {
        return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::ProviderRejected);
      }
    }
    return TypedQueryResult<Value>::value(values[0].value() * 1000 + values[1].value() * 100 +
                                          values[2].value() * 10 + values[3].value());
  }
};

struct ParallelSlowLeafQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "ParallelSlowLeafQuery"_zcc, "test.query.parallel-slow-leaf"_zcc, ReuseClass::Semantic,
      RetentionClass::Retained,    QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext&, const Key& key) {
    usleep(100000);
    return TypedQueryResult<Value>::value(key);
  }
};

struct ParallelSlowSumQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "ParallelSlowSumQuery"_zcc, "test.query.parallel-slow-sum"_zcc,  ReuseClass::Semantic,
      RetentionClass::Retained,   QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    uint32_t keys[] = {key + 3, key + 1, key + 2, key};
    auto values = context.getParallel<ParallelSlowLeafQuery>(zc::arrayPtr(keys));
    if (values.size() != 4) {
      return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::ProviderRejected);
    }
    uint32_t sum = 0;
    for (const auto& value : values) {
      if (value.isRuntimeFailure()) {
        return TypedQueryResult<Value>::runtimeFailure(value.runtimeFailure());
      }
      sum += value.value();
    }
    return TypedQueryResult<Value>::value(sum);
  }
};

struct ParallelTrackedSlowLeafQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "ParallelTrackedSlowLeafQuery"_zcc,
      "test.query.parallel-tracked-slow-leaf"_zcc,
      ReuseClass::Semantic,
      RetentionClass::Retained,
      QueryEqualityPolicy::CanonicalBytes,
      QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto input = context.get<LowInput>(key);
    if (input.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(input.runtimeFailure());
    }
    usleep(100000);
    return TypedQueryResult<Value>::value(input.value());
  }
};

struct ParallelTrackedSumQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"ParallelTrackedSumQuery"_zcc,
                                                         "test.query.parallel-tracked-sum"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    uint32_t keys[] = {key + 3, key + 1, key + 2, key};
    auto values = context.getParallel<ParallelTrackedSlowLeafQuery>(zc::arrayPtr(keys));
    if (values.size() != 4) {
      return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::ProviderRejected);
    }
    uint32_t sum = 0;
    for (const auto& value : values) {
      if (value.isRuntimeFailure()) {
        return TypedQueryResult<Value>::runtimeFailure(value.runtimeFailure());
      }
      sum += value.value();
    }
    return TypedQueryResult<Value>::value(sum);
  }
};

struct NestedParallelLeafQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"NestedParallelLeafQuery"_zcc,
                                                         "test.query.nested-parallel-leaf"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    uint32_t keys[] = {key, key + 1};
    auto values = context.getParallel<ParallelSlowLeafQuery>(zc::arrayPtr(keys));
    if (values.size() == 0 || values[0].isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(
          values.size() == 0 ? QueryRuntimeFailure::ProviderRejected : values[0].runtimeFailure());
    }
    return TypedQueryResult<Value>::value(values[0].value());
  }
};

struct NestedParallelRootQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"NestedParallelRootQuery"_zcc,
                                                         "test.query.nested-parallel-root"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    uint32_t keys[] = {key, key + 1};
    auto values = context.getParallel<NestedParallelLeafQuery>(zc::arrayPtr(keys));
    if (values.size() == 0 || values[0].isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(
          values.size() == 0 ? QueryRuntimeFailure::ProviderRejected : values[0].runtimeFailure());
    }
    return TypedQueryResult<Value>::value(values[0].value());
  }
};

struct DeterministicAlternativeQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"DeterministicAlternativeQuery"_zcc,
                                                         "test.query.alternatives"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto control = context.get<LowInput>(key);
    if (control.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(control.runtimeFailure());
    }
    if (control.value() == 0) { return TypedQueryResult<Value>::absence(); }
    if (control.value() == 1) {
      return TypedQueryResult<Value>::semanticFailure(encodeUint32(0xdead));
    }
    return TypedQueryResult<Value>::value(control.value());
  }
  static bool verify(QueryContext&, const Key&, const TypedQueryResult<Value>&) { return true; }
};

struct VerifiedQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "VerifiedQuery"_zcc,      "test.query.verified"_zcc,           ReuseClass::Semantic,
      RetentionClass::Retained, QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext&, const Key& key) {
    return TypedQueryResult<Value>::value(key);
  }
  static bool verify(QueryContext& context, const Key&, const TypedQueryResult<Value>& result) {
    auto verifierInput = context.get<HighInput>(90);
    return !verifierInput.isRuntimeFailure() && verifierInput.value() != 0 &&
           result.kind() == QueryValueKind::Value;
  }
};

struct HighOnlyQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "HighOnlyQuery"_zcc,      "test.query.high-only"_zcc,          ReuseClass::Semantic,
      RetentionClass::Retained, QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto input = context.get<HighInput>(key);
    if (input.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(input.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(input.value());
  }
};

struct OptionalLowInputQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "OptionalLowInputQuery"_zcc, "test.query.optional-low-input"_zcc, ReuseClass::Semantic,
      RetentionClass::Evictable,   QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto optional = context.probeInput<LowInput>(key);
    if (optional.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(optional.runtimeFailure());
    }
    auto fallback = context.get<HighInput>(100);
    if (fallback.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(fallback.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(optional.kind() == QueryValueKind::Absence
                                              ? fallback.value()
                                              : optional.value() + fallback.value());
  }
};

struct InvalidDerivedInputProbeQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "InvalidDerivedInputProbeQuery"_zcc,
      "test.query.invalid-derived-input-probe"_zcc,
      ReuseClass::Semantic,
      RetentionClass::Retained,
      QueryEqualityPolicy::CanonicalBytes,
      QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto invalid = context.probeInput<AddTenQuery>(key);
    if (invalid.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(invalid.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(invalid.value());
  }
};

struct DurabilitySwitchQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "DurabilitySwitchQuery"_zcc, "test.query.durability-switch"_zcc,  ReuseClass::Semantic,
      RetentionClass::Retained,    QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key&) {
    auto control = context.get<HighInput>(9);
    auto stable = context.get<HighInput>(10);
    if (control.isRuntimeFailure() || stable.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::ProviderRejected);
    }
    if (control.value() != 0) {
      auto low = context.get<LowInput>(10);
      if (low.isRuntimeFailure()) {
        return TypedQueryResult<Value>::runtimeFailure(low.runtimeFailure());
      }
    }
    return TypedQueryResult<Value>::value(stable.value());
  }
};

struct SlowQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"SlowQuery"_zcc,
                                                         "test.query.slow"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    for (uint32_t iteration = 0; iteration < 50; ++iteration) {
      if (context.isCancelled()) {
        return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::Cancelled);
      }
      usleep(1000);
    }
    return TypedQueryResult<Value>::value(key + 1);
  }
};

struct CycleBQuery;

struct CycleAQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"CycleAQuery"_zcc,
                                                         "test.query.cycle-a"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key);
};

struct CycleBQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{"CycleBQuery"_zcc,
                                                         "test.query.cycle-b"_zcc,
                                                         ReuseClass::Semantic,
                                                         RetentionClass::Retained,
                                                         QueryEqualityPolicy::CanonicalBytes,
                                                         QueryCyclePolicy::Reject,
                                                         QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto cycle = context.get<CycleAQuery>(key);
    if (cycle.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(cycle.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(cycle.value());
  }
};

inline TypedQueryResult<CycleAQuery::Value> CycleAQuery::provide(QueryContext& context,
                                                                 const Key& key) {
  auto cycle = context.get<CycleBQuery>(key);
  if (cycle.isRuntimeFailure()) {
    return TypedQueryResult<Value>::runtimeFailure(cycle.runtimeFailure());
  }
  return TypedQueryResult<Value>::value(cycle.value());
}

struct CrossWorkerCycleBQuery;

struct CrossWorkerCycleAQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "CrossWorkerCycleAQuery"_zcc, "test.query.cross-cycle-a"_zcc,      ReuseClass::Semantic,
      RetentionClass::Retained,     QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key);
};

struct CrossWorkerCycleBQuery final : AddTenQuery {
  static constexpr SemanticDescriptorMetadata descriptor{
      "CrossWorkerCycleBQuery"_zcc, "test.query.cross-cycle-b"_zcc,      ReuseClass::Semantic,
      RetentionClass::Retained,     QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject,
      QueryCostClass::Linear};
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    usleep(20000);
    auto cycle = context.get<CrossWorkerCycleAQuery>(key);
    if (cycle.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(cycle.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(cycle.value());
  }
};

inline TypedQueryResult<CrossWorkerCycleAQuery::Value> CrossWorkerCycleAQuery::provide(
    QueryContext& context, const Key& key) {
  usleep(20000);
  auto cycle = context.get<CrossWorkerCycleBQuery>(key);
  if (cycle.isRuntimeFailure()) {
    return TypedQueryResult<Value>::runtimeFailure(cycle.runtimeFailure());
  }
  return TypedQueryResult<Value>::value(cycle.value());
}

inline void registerCoreKinds(QueryDatabase& database) {
  const auto requireRegistration = []<typename Descriptor>(QueryDatabase& target) {
    ZC_IREQUIRE(target.registerDescriptor<Descriptor>().isRegistered(),
                "failed to register query test descriptor");
  };
  requireRegistration.template operator()<LowInput>(database);
  requireRegistration.template operator()<MediumInput>(database);
  requireRegistration.template operator()<HighInput>(database);
  requireRegistration.template operator()<FrozenInput>(database);
  requireRegistration.template operator()<MalformedLowInputKey>(database);
  requireRegistration.template operator()<AddTenQuery>(database);
  requireRegistration.template operator()<ParityProjectionQuery>(database);
  requireRegistration.template operator()<ChangedProjectionQuery>(database);
  requireRegistration.template operator()<EvictableQuery>(database);
  requireRegistration.template operator()<BranchQuery>(database);
  requireRegistration.template operator()<ParallelSumQuery>(database);
  requireRegistration.template operator()<ParallelPositionalQuery>(database);
  requireRegistration.template operator()<ParallelSlowLeafQuery>(database);
  requireRegistration.template operator()<ParallelSlowSumQuery>(database);
  requireRegistration.template operator()<ParallelTrackedSlowLeafQuery>(database);
  requireRegistration.template operator()<ParallelTrackedSumQuery>(database);
  requireRegistration.template operator()<NestedParallelLeafQuery>(database);
  requireRegistration.template operator()<NestedParallelRootQuery>(database);
  requireRegistration.template operator()<DeterministicAlternativeQuery>(database);
  requireRegistration.template operator()<VerifiedQuery>(database);
  requireRegistration.template operator()<HighOnlyQuery>(database);
  requireRegistration.template operator()<OptionalLowInputQuery>(database);
  requireRegistration.template operator()<InvalidDerivedInputProbeQuery>(database);
  requireRegistration.template operator()<DurabilitySwitchQuery>(database);
  requireRegistration.template operator()<SlowQuery>(database);
  requireRegistration.template operator()<CycleAQuery>(database);
  requireRegistration.template operator()<CycleBQuery>(database);
  requireRegistration.template operator()<CrossWorkerCycleAQuery>(database);
  requireRegistration.template operator()<CrossWorkerCycleBQuery>(database);
}

inline InputTransaction beginTransaction(QueryDatabase& database) {
  auto transaction = database.beginInputTransaction(database.snapshot().revision());
  ZC_IREQUIRE(transaction.isOpened(), "failed to open query test input transaction");
  return zc::mv(transaction).takeTransaction();
}

inline bool hasEvent(zc::ArrayPtr<const QueryEvent> events, QueryEventKind kind) {
  for (const auto& event : events) {
    if (event.kind() == kind) { return true; }
  }
  return false;
}

/// \brief Narrow native-test bridge for real erased request results and the one-shot seal gate.
class QueryRuntimeTestAccess final {
public:
  template <typename Descriptor>
  static QueryRequestResult evaluate(const QuerySnapshot& snapshot,
                                     const typename Descriptor::Key& key) {
    CancellationSource cancellation;
    return snapshot.getEncoded(Descriptor::descriptor.domain, Descriptor::encodeKey(key),
                               cancellation.token());
  }

  template <typename Descriptor>
  static CapabilityDemandResult<Descriptor> decode(QueryRequestResult&& result,
                                                   const QuerySnapshot& snapshot) {
    return CapabilityResultDecoder<Descriptor>::decode(zc::mv(result), snapshot.databaseIdentity(),
                                                       snapshot.revision());
  }

  static void armFinalSealPhaseTwoGate(QueryDatabase& database) {
    database.armFinalSealPhaseTwoGateForTest();
  }

  static void waitForFinalSealPhaseTwoGate(QueryDatabase& database) {
    database.waitForFinalSealPhaseTwoGateForTest();
  }

  static void releaseFinalSealPhaseTwoGate(QueryDatabase& database) {
    database.releaseFinalSealPhaseTwoGateForTest();
  }
};

}  // namespace zomlang::compiler::query::test
