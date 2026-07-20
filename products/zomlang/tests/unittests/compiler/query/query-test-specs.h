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
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::query::test {

inline basic::ThreadPool& queryTestScheduler() {
  static basic::ThreadPool scheduler(4);
  return scheduler;
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

inline QueryKindContract inputContract(zc::StringPtr domain, Durability durability) {
  auto contract = QueryKindContract::input(domain, 1, 1, durability);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

inline QueryKindContract derivedContract(zc::StringPtr domain,
                                         ReuseClass reuse = ReuseClass::Semantic,
                                         RetentionClass retention = RetentionClass::Retained) {
  auto contract = QueryKindContract::derived(domain, 1, 1, reuse, retention);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

struct LowInput {
  using Key = uint32_t;
  using Value = uint32_t;
  static zc::StringPtr domain() { return "test.input.low"_zc; }
  static QueryKindContract contract() { return inputContract(domain(), Durability::Low); }
  static zc::Array<uint8_t> encodeKey(const Key& key) { return encodeUint32(key); }
  static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes) { return decodeUint32(bytes); }
  static zc::Array<uint8_t> encodeValue(const Value& value) { return encodeUint32(value); }
  static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
    return decodeUint32(bytes);
  }
};

struct MediumInput final : LowInput {
  static zc::StringPtr domain() { return "test.input.medium"_zc; }
  static QueryKindContract contract() { return inputContract(domain(), Durability::Medium); }
};

struct HighInput final : LowInput {
  static zc::StringPtr domain() { return "test.input.high"_zc; }
  static QueryKindContract contract() { return inputContract(domain(), Durability::High); }
};

struct FrozenInput final : LowInput {
  static zc::StringPtr domain() { return "test.input.frozen"_zc; }
  static QueryKindContract contract() { return inputContract(domain(), Durability::Frozen); }
};

struct AddTenQuery {
  using Key = uint32_t;
  using Value = uint32_t;
  static zc::StringPtr domain() { return "test.query.add-ten"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.parity"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto source = context.get<AddTenQuery>(key);
    if (source.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(source.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(source.value() & 1U);
  }
};

struct RevisionLocalQuery final : AddTenQuery {
  static zc::StringPtr domain() { return "test.query.revision-local"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::RevisionLocal);
  }
};

struct EvictableQuery final : AddTenQuery {
  static zc::StringPtr domain() { return "test.query.evictable"_zc; }
  static QueryKindContract contract() {
    return derivedContract(domain(), ReuseClass::Semantic, RetentionClass::Evictable);
  }
};

struct BranchQuery final : AddTenQuery {
  static zc::StringPtr domain() { return "test.query.branch"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.parallel-sum"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.parallel-positional"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.parallel-slow-leaf"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
  static TypedQueryResult<Value> provide(QueryContext&, const Key& key) {
    usleep(100000);
    return TypedQueryResult<Value>::value(key);
  }
};

struct ParallelSlowSumQuery final : AddTenQuery {
  static zc::StringPtr domain() { return "test.query.parallel-slow-sum"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.parallel-tracked-slow-leaf"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.parallel-tracked-sum"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.nested-parallel-leaf"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.nested-parallel-root"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.alternatives"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.verified"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.high-only"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key) {
    auto input = context.get<HighInput>(key);
    if (input.isRuntimeFailure()) {
      return TypedQueryResult<Value>::runtimeFailure(input.runtimeFailure());
    }
    return TypedQueryResult<Value>::value(input.value());
  }
};

struct DurabilitySwitchQuery final : AddTenQuery {
  static zc::StringPtr domain() { return "test.query.durability-switch"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.slow"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.cycle-a"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key);
};

struct CycleBQuery final : AddTenQuery {
  static zc::StringPtr domain() { return "test.query.cycle-b"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  static zc::StringPtr domain() { return "test.query.cross-cycle-a"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
  static TypedQueryResult<Value> provide(QueryContext& context, const Key& key);
};

struct CrossWorkerCycleBQuery final : AddTenQuery {
  static zc::StringPtr domain() { return "test.query.cross-cycle-b"_zc; }
  static QueryKindContract contract() { return derivedContract(domain()); }
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
  ZC_IREQUIRE(database.registerInputKind<LowInput>() != zc::none, "failed to register LowInput");
  ZC_IREQUIRE(database.registerInputKind<MediumInput>() != zc::none,
              "failed to register MediumInput");
  ZC_IREQUIRE(database.registerInputKind<HighInput>() != zc::none, "failed to register HighInput");
  ZC_IREQUIRE(database.registerInputKind<FrozenInput>() != zc::none,
              "failed to register FrozenInput");
  ZC_IREQUIRE(database.registerDerivedKind<AddTenQuery>() != zc::none,
              "failed to register AddTenQuery");
  ZC_IREQUIRE(database.registerDerivedKind<ParityProjectionQuery>() != zc::none,
              "failed to register ParityProjectionQuery");
  ZC_IREQUIRE(database.registerDerivedKind<RevisionLocalQuery>() != zc::none,
              "failed to register RevisionLocalQuery");
  ZC_IREQUIRE(database.registerDerivedKind<EvictableQuery>() != zc::none,
              "failed to register EvictableQuery");
  ZC_IREQUIRE(database.registerDerivedKind<BranchQuery>() != zc::none,
              "failed to register BranchQuery");
  ZC_IREQUIRE(database.registerDerivedKind<ParallelSumQuery>() != zc::none,
              "failed to register ParallelSumQuery");
  ZC_IREQUIRE(database.registerDerivedKind<ParallelPositionalQuery>() != zc::none,
              "failed to register ParallelPositionalQuery");
  ZC_IREQUIRE(database.registerDerivedKind<ParallelSlowLeafQuery>() != zc::none,
              "failed to register ParallelSlowLeafQuery");
  ZC_IREQUIRE(database.registerDerivedKind<ParallelSlowSumQuery>() != zc::none,
              "failed to register ParallelSlowSumQuery");
  ZC_IREQUIRE(database.registerDerivedKind<ParallelTrackedSlowLeafQuery>() != zc::none,
              "failed to register ParallelTrackedSlowLeafQuery");
  ZC_IREQUIRE(database.registerDerivedKind<ParallelTrackedSumQuery>() != zc::none,
              "failed to register ParallelTrackedSumQuery");
  ZC_IREQUIRE(database.registerDerivedKind<NestedParallelLeafQuery>() != zc::none,
              "failed to register NestedParallelLeafQuery");
  ZC_IREQUIRE(database.registerDerivedKind<NestedParallelRootQuery>() != zc::none,
              "failed to register NestedParallelRootQuery");
  ZC_IREQUIRE(database.registerDerivedKind<DeterministicAlternativeQuery>() != zc::none,
              "failed to register DeterministicAlternativeQuery");
  ZC_IREQUIRE(database.registerDerivedKind<VerifiedQuery>() != zc::none,
              "failed to register VerifiedQuery");
  ZC_IREQUIRE(database.registerDerivedKind<HighOnlyQuery>() != zc::none,
              "failed to register HighOnlyQuery");
  ZC_IREQUIRE(database.registerDerivedKind<DurabilitySwitchQuery>() != zc::none,
              "failed to register DurabilitySwitchQuery");
  ZC_IREQUIRE(database.registerDerivedKind<SlowQuery>() != zc::none,
              "failed to register SlowQuery");
  ZC_IREQUIRE(database.registerDerivedKind<CycleAQuery>() != zc::none,
              "failed to register CycleAQuery");
  ZC_IREQUIRE(database.registerDerivedKind<CycleBQuery>() != zc::none,
              "failed to register CycleBQuery");
  ZC_IREQUIRE(database.registerDerivedKind<CrossWorkerCycleAQuery>() != zc::none,
              "failed to register CrossWorkerCycleAQuery");
  ZC_IREQUIRE(database.registerDerivedKind<CrossWorkerCycleBQuery>() != zc::none,
              "failed to register CrossWorkerCycleBQuery");
}

inline InputTransaction beginTransaction(QueryDatabase& database) {
  auto transaction = database.beginInputTransaction();
  return zc::mv(ZC_REQUIRE_NONNULL(transaction));
}

inline bool hasEvent(zc::ArrayPtr<const QueryEvent> events, QueryEventKind kind) {
  for (const auto& event : events) {
    if (event.kind() == kind) { return true; }
  }
  return false;
}

}  // namespace zomlang::compiler::query::test
