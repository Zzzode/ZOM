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

#include "zomlang/compiler/query/query-database.h"

#include "zc/core/debug.h"
#include "zc/core/exception.h"
#include "zc/core/mutex.h"
#include "zc/core/refcount.h"
#include "zc/core/time.h"
#include "zomlang/compiler/basic/thread-pool.h"

namespace zomlang::compiler::query {
namespace {

constexpr uint32_t kShaInitialState[] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

constexpr uint32_t kShaRoundConstants[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t rotateRight(uint32_t value, uint32_t count) noexcept {
  return (value >> count) | (value << (32 - count));
}

uint32_t readUint32(zc::ArrayPtr<const uint8_t> bytes) {
  return (uint32_t{bytes[0]} << 24) | (uint32_t{bytes[1]} << 16) | (uint32_t{bytes[2]} << 8) |
         uint32_t{bytes[3]};
}

void writeUint32(zc::ArrayPtr<uint8_t> output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value >> 24);
  output[1] = static_cast<uint8_t>(value >> 16);
  output[2] = static_cast<uint8_t>(value >> 8);
  output[3] = static_cast<uint8_t>(value);
}

void compressSha(zc::ArrayPtr<uint32_t> state, zc::ArrayPtr<const uint8_t> block) {
  uint32_t schedule[64];
  for (uint32_t index = 0; index < 16; ++index) {
    schedule[index] = readUint32(block.slice(index * 4, index * 4 + 4));
  }
  for (uint32_t index = 16; index < 64; ++index) {
    const uint32_t previous15 = schedule[index - 15];
    const uint32_t previous2 = schedule[index - 2];
    const uint32_t sigma0 =
        rotateRight(previous15, 7) ^ rotateRight(previous15, 18) ^ (previous15 >> 3);
    const uint32_t sigma1 =
        rotateRight(previous2, 17) ^ rotateRight(previous2, 19) ^ (previous2 >> 10);
    schedule[index] = schedule[index - 16] + sigma0 + schedule[index - 7] + sigma1;
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];
  for (uint32_t index = 0; index < 64; ++index) {
    const uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t temporary1 = h + sum1 + choice + kShaRoundConstants[index] + schedule[index];
    const uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temporary2 = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

QueryKeyFingerprint sha256(zc::ArrayPtr<const uint8_t> input) {
  uint32_t state[8];
  for (size_t index = 0; index < 8; ++index) { state[index] = kShaInitialState[index]; }
  size_t offset = 0;
  while (input.size() - offset >= 64) {
    compressSha(zc::arrayPtr(state), input.slice(offset, offset + 64));
    offset += 64;
  }
  uint8_t tail[128] = {};
  const size_t remainder = input.size() - offset;
  for (size_t index = 0; index < remainder; ++index) { tail[index] = input[offset + index]; }
  tail[remainder] = 0x80;
  const size_t tailSize = remainder < 56 ? 64 : 128;
  uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8;
  for (size_t index = 0; index < 8; ++index) {
    tail[tailSize - 1 - index] = static_cast<uint8_t>(bitLength);
    bitLength >>= 8;
  }
  compressSha(zc::arrayPtr(state), zc::arrayPtr(tail, tailSize).first(64));
  if (tailSize == 128) {
    compressSha(zc::arrayPtr(state), zc::arrayPtr(tail, tailSize).slice(64, 128));
  }
  uint8_t digest[32];
  for (size_t index = 0; index < 8; ++index) {
    writeUint32(zc::arrayPtr(digest).slice(index * 4, index * 4 + 4), state[index]);
  }
  return ZC_REQUIRE_NONNULL(QueryKeyFingerprint::fromBytes(zc::arrayPtr(digest)));
}

void appendUint32(zc::Vector<uint8_t>& bytes, uint32_t value) {
  bytes.add(static_cast<uint8_t>(value >> 24));
  bytes.add(static_cast<uint8_t>(value >> 16));
  bytes.add(static_cast<uint8_t>(value >> 8));
  bytes.add(static_cast<uint8_t>(value));
}

zc::Array<uint8_t> vectorToArray(zc::Vector<uint8_t>&& bytes) {
  auto result = zc::heapArray<uint8_t>(bytes.size());
  for (size_t index = 0; index < bytes.size(); ++index) { result[index] = bytes[index]; }
  return result;
}

Durability lowerDurability(Durability left, Durability right) noexcept {
  return static_cast<uint8_t>(left) < static_cast<uint8_t>(right) ? left : right;
}

bool isCanonicalDomain(zc::StringPtr domain) {
  if (domain.size() == 0 || domain.size() > UINT32_MAX) { return false; }
  for (char value : domain) {
    const bool lowercase = value >= 'a' && value <= 'z';
    const bool digit = value >= '0' && value <= '9';
    if (!lowercase && !digit && value != '.' && value != '-') { return false; }
  }
  return true;
}

bool isPrintableAscii(zc::StringPtr value) {
  if (value.size() == 0) { return false; }
  for (char character : value) {
    if (character < 0x20 || character > 0x7e) { return false; }
  }
  return true;
}

bool descriptorMetadataIsValid(const QueryDescriptorInventoryRow& row) {
  if (!isPrintableAscii(row.descriptorType) || !isPrintableAscii(row.name) ||
      !isCanonicalDomain(row.domain) || !isPrintableAscii(row.ownerPathFamily) ||
      row.equality != QueryEqualityPolicy::CanonicalBytes ||
      row.cycle != QueryCyclePolicy::Reject || row.cost != QueryCostClass::Linear) {
    return false;
  }
  switch (row.kind) {
    case QueryDescriptorKind::Input:
      return row.reuse == ReuseClass::Input && row.retention == RetentionClass::Retained &&
             row.admission == CapabilityAdmission::AnySnapshot &&
             row.failureProjection == FinalFailureProjection::None;
    case QueryDescriptorKind::Semantic:
      return (row.reuse == ReuseClass::Semantic || row.reuse == ReuseClass::Persisted) &&
             row.durability == Durability::Frozen &&
             row.admission == CapabilityAdmission::AnySnapshot &&
             row.failureProjection == FinalFailureProjection::None &&
             row.role == QueryDescriptorRole::Ordinary;
    case QueryDescriptorKind::RevisionLocalCapability:
      return row.reuse == ReuseClass::RevisionLocal && row.retention == RetentionClass::Retained &&
             row.durability == Durability::Frozen && row.role == QueryDescriptorRole::Ordinary &&
             (row.admission == CapabilityAdmission::FinalSealedSnapshot ||
              row.failureProjection == FinalFailureProjection::None);
  }
  return false;
}

bool descriptorRowsEqual(const QueryDescriptorInventoryRow& left,
                         const QueryDescriptorInventoryRow& right) {
  return left.ordinal == right.ordinal && left.descriptorType == right.descriptorType &&
         left.name == right.name && left.domain == right.domain && left.kind == right.kind &&
         left.role == right.role && left.reuse == right.reuse &&
         left.retention == right.retention && left.durability == right.durability &&
         left.equality == right.equality && left.cycle == right.cycle && left.cost == right.cost &&
         left.admission == right.admission && left.failureProjection == right.failureProjection &&
         left.ownerPathFamily == right.ownerPathFamily;
}

bool keyFingerprintMatches(const CanonicalQueryKey& left, const CanonicalQueryKey& right) {
  return left.kind() == right.kind() && left.fingerprint() == right.fingerprint();
}

class CancellationState final : public zc::AtomicRefcounted {
public:
  zc::MutexGuarded<bool> cancelled{false};
};

struct InputEntry final {
  InputEntry(CanonicalQueryKey&& key, QueryValue&& value, DatabaseRevision changedAt,
             Durability durability) noexcept
      : key(zc::mv(key)), value(zc::mv(value)), changedAt(changedAt), durability(durability) {}
  InputEntry(InputEntry&&) noexcept = default;
  InputEntry& operator=(InputEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(InputEntry);

  InputEntry clone() const { return InputEntry(key.clone(), value.clone(), changedAt, durability); }

  CanonicalQueryKey key;
  QueryValue value;
  DatabaseRevision changedAt;
  Durability durability;
};

struct Memo final {
  Memo(CanonicalQueryKey&& key, zc::Maybe<QueryValue>&& value,
       zc::Arc<RevisionLocalCapabilityMemoBase>&& capability, MemoMetadata metadata,
       zc::Vector<DependencyGroup>&& dependencies) noexcept
      : key(zc::mv(key)),
        value(zc::mv(value)),
        capability(zc::mv(capability)),
        metadata(metadata),
        dependencies(zc::mv(dependencies)) {}
  Memo(Memo&&) noexcept = default;
  Memo& operator=(Memo&&) noexcept = default;
  ZC_DISALLOW_COPY(Memo);

  Memo clone() const {
    zc::Vector<DependencyGroup> clonedDependencies;
    for (const auto& group : dependencies) { clonedDependencies.add(group.clone()); }
    zc::Maybe<QueryValue> clonedValue;
    ZC_IF_SOME(retainedValue, value) { clonedValue = retainedValue.clone(); }
    zc::Arc<RevisionLocalCapabilityMemoBase> clonedCapability;
    if (capability != nullptr) { clonedCapability = capability.addRef(); }
    return Memo(key.clone(), zc::mv(clonedValue), zc::mv(clonedCapability), metadata,
                zc::mv(clonedDependencies));
  }

  CanonicalQueryKey key;
  zc::Maybe<QueryValue> value;
  zc::Arc<RevisionLocalCapabilityMemoBase> capability;
  MemoMetadata metadata;
  zc::Vector<DependencyGroup> dependencies;
};

struct FlightData final {
  explicit FlightData(CancellationSource::Token&& evaluator) noexcept
      : evaluator(zc::mv(evaluator)) {}

  zc::Maybe<QueryRequestResult> result;
  CancellationSource::Token evaluator;
  zc::Vector<CancellationSource::Token> waiters;
  CancellationSource aggregate;
};

class Flight final : public zc::AtomicRefcounted {
public:
  Flight(CanonicalQueryKey&& key, CancellationSource::Token&& evaluator)
      : key(zc::mv(key)), data(zc::mv(evaluator)) {}

  void addRequester(const CancellationSource::Token& requester) const {
    data.lockExclusive()->waiters.add(requester.clone());
  }

  bool isCancelled() const {
    auto locked = data.lockExclusive();
    bool allCancelled = locked->evaluator.isCancelled();
    for (const auto& waiter : locked->waiters) {
      if (!waiter.isCancelled()) {
        allCancelled = false;
        break;
      }
    }
    if (allCancelled) { locked->aggregate.cancel(); }
    return allCancelled;
  }

  CancellationSource::Token cancellationToken() const {
    isCancelled();
    return data.lockShared()->aggregate.token();
  }

  CanonicalQueryKey key;
  zc::MutexGuarded<FlightData> data;
};

struct WaitEdge final {
  WaitEdge(CanonicalQueryKey&& from, CanonicalQueryKey&& to) noexcept
      : from(zc::mv(from)), to(zc::mv(to)) {}
  WaitEdge(WaitEdge&&) noexcept = default;
  WaitEdge& operator=(WaitEdge&&) noexcept = default;
  ZC_DISALLOW_COPY(WaitEdge);

  CanonicalQueryKey from;
  CanonicalQueryKey to;
};

struct SnapshotRuntime final {
  zc::Vector<InputEntry> inputs;
  zc::Vector<Memo> memos;
  zc::Vector<zc::Arc<Flight>> flights;
  zc::Vector<WaitEdge> waitEdges;
  zc::Vector<QueryEvent> events;
};

class SnapshotState final : public zc::AtomicRefcounted {
public:
  SnapshotState(QueryDatabaseIdentity&& database, DatabaseRevision revision,
                zc::ArrayPtr<const DatabaseRevision> changes,
                zc::Arc<SemanticContextCapabilityArena>&& semanticContextArena)
      : database(zc::mv(database)),
        revision(revision),
        capabilityArena(zc::arc<SnapshotCapabilityArena>(revision, zc::mv(semanticContextArena))) {
    ZC_IREQUIRE(changes.size() == 4, "snapshot durability revision count is not four");
    for (size_t index = 0; index < 4; ++index) { lastChanged[index] = changes[index]; }
  }

  QueryDatabaseIdentity database;
  DatabaseRevision revision;
  DatabaseRevision lastChanged[4];
  zc::Arc<SnapshotCapabilityArena> capabilityArena;
  zc::MutexGuarded<SnapshotRuntime> runtime;
};

struct RegisteredKind final {
  RegisteredKind(const QueryDescriptorInventoryRow& descriptor,
                 QueryDatabase::ErasedKeyValidator&& keyValidator,
                 zc::Maybe<QueryDatabase::ErasedProvider>&& provider,
                 zc::Maybe<QueryDatabase::ErasedVerifier>&& verifier,
                 zc::Maybe<QueryDatabase::ErasedCapabilityEvaluator>&& capabilityEvaluator) noexcept
      : descriptor(descriptor),
        keyValidator(zc::mv(keyValidator)),
        provider(zc::mv(provider)),
        verifier(zc::mv(verifier)),
        capabilityEvaluator(zc::mv(capabilityEvaluator)) {}
  RegisteredKind(RegisteredKind&&) noexcept = default;
  RegisteredKind& operator=(RegisteredKind&&) noexcept = default;
  ZC_DISALLOW_COPY(RegisteredKind);

  QueryDescriptorInventoryRow descriptor;
  QueryDatabase::ErasedKeyValidator keyValidator;
  zc::Maybe<QueryDatabase::ErasedProvider> provider;
  zc::Maybe<QueryDatabase::ErasedVerifier> verifier;
  zc::Maybe<QueryDatabase::ErasedCapabilityEvaluator> capabilityEvaluator;
};

struct DescriptorSlot final {
  explicit DescriptorSlot(const QueryDescriptorInventoryRow& expected) : expected(expected) {}
  DescriptorSlot(DescriptorSlot&&) noexcept = default;
  DescriptorSlot& operator=(DescriptorSlot&&) noexcept = default;
  ZC_DISALLOW_COPY(DescriptorSlot);

  QueryDescriptorInventoryRow expected;
  zc::Maybe<RegisteredKind> registered;
};

class FinalSealAdmission final : public zc::AtomicRefcounted {
public:
  FinalSealAdmission(QueryDatabaseIdentity&& database, DatabaseRevision revision,
                     CanonicalQueryKey&& contextKey, FinalSnapshotClosureKind closureKind,
                     zc::Array<uint8_t>&& finalWitness) noexcept
      : database(zc::mv(database)),
        revision(revision),
        contextKey(zc::mv(contextKey)),
        closureKind(closureKind),
        finalWitness(zc::mv(finalWitness)) {}
  ~FinalSealAdmission() noexcept(false) override = default;
  ZC_DISALLOW_COPY_AND_MOVE(FinalSealAdmission);

  QueryDatabaseIdentity database;
  DatabaseRevision revision;
  CanonicalQueryKey contextKey;
  FinalSnapshotClosureKind closureKind;
  zc::Array<uint8_t> finalWitness;
};

struct DatabaseData final {
  DatabaseData(QueryDatabaseIdentity&& database, QueryDatabaseIdentity&& snapshotDatabase,
               QueryDescriptorInventoryRef descriptorInventory,
               zc::Arc<SemanticContextCapabilityArena>&& capabilityArena)
      : database(zc::mv(database)),
        descriptorInventory(descriptorInventory),
        capabilityArena(zc::mv(capabilityArena)),
        current(zc::arc<SnapshotState>(zc::mv(snapshotDatabase), DatabaseRevision(),
                                       zc::arrayPtr(initialLastChanged),
                                       this->capabilityArena.addRef())) {
    ZC_IREQUIRE(isPrintableAscii(descriptorInventory.identity()),
                "query descriptor inventory identity must be printable ASCII");
    ZC_IREQUIRE(descriptorInventory.rows().size() <= UINT32_MAX,
                "query descriptor inventory exceeds the ordinal range");
    for (size_t index = 0; index < descriptorInventory.rows().size(); ++index) {
      const auto& row = descriptorInventory.rows()[index];
      ZC_IREQUIRE(row.ordinal == index, "query descriptor inventory ordinals are not contiguous");
      ZC_IREQUIRE(descriptorMetadataIsValid(row),
                  "query descriptor inventory contains invalid metadata");
      for (size_t priorIndex = 0; priorIndex < index; ++priorIndex) {
        ZC_IREQUIRE(descriptorInventory.rows()[priorIndex].descriptorType != row.descriptorType,
                    "query descriptor inventory repeats a descriptor type");
        ZC_IREQUIRE(descriptorInventory.rows()[priorIndex].name != row.name,
                    "query descriptor inventory repeats a descriptor name");
        ZC_IREQUIRE(descriptorInventory.rows()[priorIndex].domain != row.domain,
                    "query descriptor inventory repeats a descriptor domain");
      }
      descriptors.add(DescriptorSlot(row));
    }
  }

  DatabaseRevision initialLastChanged[4] = {};

  zc::Vector<DescriptorSlot> descriptors;
  QueryDatabaseIdentity database;
  QueryDescriptorInventoryRef descriptorInventory;
  zc::Arc<SemanticContextCapabilityArena> capabilityArena;
  zc::Arc<SnapshotState> current;
  bool registrySealed = false;
  bool transactionOpen = false;
  zc::Arc<const FinalSealAdmission> finalSeal;
};

struct DetailedDemand final {
  DetailedDemand(QueryRequestResult&& result, MemoMetadata metadata) noexcept
      : result(zc::mv(result)), metadata(metadata) {}
  DetailedDemand(DetailedDemand&&) noexcept = default;
  DetailedDemand& operator=(DetailedDemand&&) noexcept = default;
  ZC_DISALLOW_COPY(DetailedDemand);

  QueryRequestResult result;
  MemoMetadata metadata;
};

struct FinalSealPhaseTwoGateState final {
  bool armed = false;
  bool claimed = false;
  bool entered = false;
  bool released = false;
};

struct ParallelDemandData final {
  explicit ParallelDemandData(size_t count) {
    for (size_t index = 0; index < count; ++index) { results.add(zc::none); }
  }

  zc::Vector<zc::Maybe<DetailedDemand>> results;
  size_t completed = 0;
};

class ParallelDemandState final : public zc::AtomicRefcounted {
public:
  explicit ParallelDemandState(size_t count) : data(count) {}

  zc::MutexGuarded<ParallelDemandData> data;
};

struct StagedInput final {
  StagedInput(InputEntry&& current, QueryValue&& baseValue, bool changed, bool existedAtBase,
              bool present, bool operated) noexcept
      : current(zc::mv(current)),
        baseValue(zc::mv(baseValue)),
        changed(changed),
        existedAtBase(existedAtBase),
        present(present),
        operated(operated) {}
  StagedInput(StagedInput&&) noexcept = default;
  StagedInput& operator=(StagedInput&&) noexcept = default;
  ZC_DISALLOW_COPY(StagedInput);

  void refreshChanged() {
    changed = present != existedAtBase || (present && existedAtBase && current.value != baseValue);
  }

  InputEntry current;
  QueryValue baseValue;
  bool changed;
  bool existedAtBase;
  bool present;
  bool operated;
};

zc::Maybe<size_t> exactInputIndex(const SnapshotRuntime& runtime, const CanonicalQueryKey& key,
                                  bool& collision) {
  for (size_t index = 0; index < runtime.inputs.size(); ++index) {
    if (!keyFingerprintMatches(runtime.inputs[index].key, key)) { continue; }
    if (runtime.inputs[index].key == key) { return index; }
    collision = true;
    return zc::none;
  }
  return zc::none;
}

zc::Maybe<size_t> exactMemoIndex(const SnapshotRuntime& runtime, const CanonicalQueryKey& key,
                                 bool& collision) {
  for (size_t index = 0; index < runtime.memos.size(); ++index) {
    if (!keyFingerprintMatches(runtime.memos[index].key, key)) { continue; }
    if (runtime.memos[index].key == key) { return index; }
    collision = true;
    return zc::none;
  }
  return zc::none;
}

zc::Maybe<size_t> exactFlightIndex(const SnapshotRuntime& runtime, const CanonicalQueryKey& key,
                                   bool& collision) {
  for (size_t index = 0; index < runtime.flights.size(); ++index) {
    if (!keyFingerprintMatches(runtime.flights[index]->key, key)) { continue; }
    if (runtime.flights[index]->key == key) { return index; }
    collision = true;
    return zc::none;
  }
  return zc::none;
}

void replaceMemo(SnapshotRuntime& runtime, Memo&& memo) {
  bool collision = false;
  ZC_IF_SOME(index, exactMemoIndex(runtime, memo.key, collision)) {
    runtime.memos[index] = zc::mv(memo);
    return;
  }
  ZC_IREQUIRE(!collision, "query memo fingerprint collision during publication");
  runtime.memos.add(zc::mv(memo));
}

void removeFlight(SnapshotRuntime& runtime, const Flight& flight) {
  zc::Vector<zc::Arc<Flight>> retained;
  for (const auto& candidate : runtime.flights) {
    if (candidate.get() != &flight) { retained.add(candidate.addRef()); }
  }
  runtime.flights = zc::mv(retained);
}

void removeWaitEdge(SnapshotRuntime& runtime, const CanonicalQueryKey& from,
                    const CanonicalQueryKey& to) {
  zc::Vector<WaitEdge> retained;
  bool removed = false;
  for (const auto& edge : runtime.waitEdges) {
    if (!removed && edge.from == from && edge.to == to) {
      removed = true;
      continue;
    }
    retained.add(WaitEdge(edge.from.clone(), edge.to.clone()));
  }
  runtime.waitEdges = zc::mv(retained);
}

bool pathExists(const SnapshotRuntime& runtime, const CanonicalQueryKey& from,
                const CanonicalQueryKey& target, zc::Vector<CanonicalQueryKey>& visited) {
  if (from == target) { return true; }
  for (const auto& prior : visited) {
    if (prior == from) { return false; }
  }
  visited.add(from.clone());
  for (const auto& edge : runtime.waitEdges) {
    if (edge.from == from && pathExists(runtime, edge.to, target, visited)) { return true; }
  }
  return false;
}

bool containsKey(zc::ArrayPtr<const CanonicalQueryKey> keys, const CanonicalQueryKey& key) {
  for (const auto& candidate : keys) {
    if (candidate == key) { return true; }
  }
  return false;
}

QueryEventKind failureEvent(QueryRuntimeFailure failure) {
  switch (failure) {
    case QueryRuntimeFailure::Cancelled:
      return QueryEventKind::Cancelled;
    case QueryRuntimeFailure::Cycle:
      return QueryEventKind::Cycle;
    case QueryRuntimeFailure::VerifierRejected:
      return QueryEventKind::VerifierRejected;
    case QueryRuntimeFailure::UnregisteredKind:
    case QueryRuntimeFailure::InvalidKeyEncoding:
    case QueryRuntimeFailure::MissingInput:
    case QueryRuntimeFailure::ProviderRejected:
    case QueryRuntimeFailure::FingerprintCollision:
    case QueryRuntimeFailure::InvariantViolation:
    case QueryRuntimeFailure::FinalSealRequired:
    case QueryRuntimeFailure::FinalSealMismatch:
    case QueryRuntimeFailure::AllocationFailure:
      return QueryEventKind::RuntimeFailed;
  }
  return QueryEventKind::RuntimeFailed;
}

zc::Arc<const FinalSealAdmission> retainAdmission(
    const zc::Arc<const FinalSealAdmission>& admission) {
  if (admission == nullptr) { return nullptr; }
  return admission.addRef();
}

}  // namespace

namespace _query_detail {

class FinalSealPreparationState final : public zc::AtomicRefcounted {
public:
  FinalSealPreparationState(zc::Arc<SnapshotState>&& snapshot,
                            const QueryDescriptorInventoryRow& descriptor) noexcept
      : snapshotField(zc::mv(snapshot)), descriptorField(descriptor) {}
  ~FinalSealPreparationState() noexcept(false) override = default;
  ZC_DISALLOW_COPY_AND_MOVE(FinalSealPreparationState);

  zc::Arc<SnapshotState> snapshotField;
  QueryDescriptorInventoryRow descriptorField;
};

FinalSealPreparation::FinalSealPreparation(
    QueryDatabaseIdentity&& database, DatabaseRevision revision, CanonicalQueryKey&& contextKey,
    zc::Arc<const FinalSealPreparationState>&& state) noexcept
    : databaseField(zc::mv(database)),
      revisionField(revision),
      contextKeyField(zc::mv(contextKey)),
      stateField(zc::mv(state)) {}
FinalSealPreparation::FinalSealPreparation(FinalSealPreparation&&) noexcept = default;
FinalSealPreparation& FinalSealPreparation::operator=(FinalSealPreparation&&) noexcept = default;
FinalSealPreparation::~FinalSealPreparation() noexcept(false) = default;

}  // namespace _query_detail

struct CancellationSource::Impl final {
  Impl() : state(zc::arc<CancellationState>()) {}
  zc::Arc<CancellationState> state;
};

struct CancellationSource::Token::Impl final {
  explicit Impl(zc::Arc<CancellationState>&& state) : state(zc::mv(state)) {}
  zc::Arc<CancellationState> state;
};

CancellationSource::CancellationSource() : impl(zc::heap<Impl>()) {}
CancellationSource::~CancellationSource() noexcept(false) = default;
CancellationSource::CancellationSource(CancellationSource&&) noexcept = default;
CancellationSource& CancellationSource::operator=(CancellationSource&&) noexcept = default;

CancellationSource::Token CancellationSource::token() const {
  return Token(zc::heap<Token::Impl>(impl->state.addRef()));
}

void CancellationSource::cancel() { *impl->state->cancelled.lockExclusive() = true; }

CancellationSource::Token::Token(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
CancellationSource::Token::Token(Token&&) noexcept = default;
CancellationSource::Token& CancellationSource::Token::operator=(Token&&) noexcept = default;
CancellationSource::Token::~Token() noexcept(false) = default;

CancellationSource::Token CancellationSource::Token::clone() const {
  return Token(zc::heap<Impl>(impl->state.addRef()));
}

bool CancellationSource::Token::isCancelled() const { return *impl->state->cancelled.lockShared(); }

struct QueryDatabase::Impl final {
  Impl(basic::ThreadPool& scheduler, QueryDescriptorInventoryRef descriptorInventory,
       zc::Arc<SemanticContextCapabilityArena>&& capabilityArena) noexcept
      : identity(QueryDatabaseIdentity::create()),
        data(DatabaseData(identity.retain(), identity.retain(), descriptorInventory,
                          zc::mv(capabilityArena))),
        scheduler(scheduler) {}

  enum class KeyAdmission : uint8_t { Trusted, Validate };

  QueryDatabaseIdentity identity;
  zc::MutexGuarded<DatabaseData> data;
  zc::MutexGuarded<size_t> liveBorrowers{0};
  zc::MutexGuarded<FinalSealPhaseTwoGateState> finalSealPhaseTwoGate;
  basic::ThreadPool& scheduler;

  void retainBorrower() { ++*liveBorrowers.lockExclusive(); }

  void releaseBorrower() {
    auto count = liveBorrowers.lockExclusive();
    ZC_IREQUIRE(*count != 0, "query database borrower count underflow");
    --*count;
  }

  void requireNoBorrowers() const {
    ZC_IREQUIRE(*liveBorrowers.lockShared() == 0,
                "query database destroyed while wrappers still borrow it");
  }

  CanonicalQueryKey makeCanonicalKey(const RegisteredKind& kind,
                                     zc::Array<uint8_t>&& keyBytes) const {
    static constexpr zc::StringPtr fingerprintDomain = "zom.query-key"_zc;
    const auto domain = kind.descriptor.domain;
    zc::Vector<uint8_t> preimage;
    for (char value : fingerprintDomain) { preimage.add(static_cast<uint8_t>(value)); }
    preimage.add(0);
    appendUint32(preimage, static_cast<uint32_t>(domain.size()));
    for (char value : domain) { preimage.add(static_cast<uint8_t>(value)); }
    appendUint32(preimage, static_cast<uint32_t>(keyBytes.size()));
    for (uint8_t value : keyBytes) { preimage.add(value); }
    auto fingerprintBytes = vectorToArray(zc::mv(preimage));
    return CanonicalQueryKey(QueryKindId(kind.descriptor.ordinal), sha256(fingerprintBytes.asPtr()),
                             zc::mv(keyBytes));
  }

  zc::Maybe<CanonicalQueryKey> makeKeyInternal(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                               QueryRuntimeFailure& failure,
                                               KeyAdmission admission) const {
    failure = QueryRuntimeFailure::UnregisteredKind;
    if (keyBytes.size() > UINT32_MAX) {
      if (admission == KeyAdmission::Validate) {
        failure = QueryRuntimeFailure::InvalidKeyEncoding;
      }
      return zc::none;
    }
    auto locked = data.lockShared();
    zc::Maybe<size_t> descriptorIndex;
    for (size_t index = 0; index < locked->descriptors.size(); ++index) {
      if (locked->descriptors[index].expected.domain == domain) {
        descriptorIndex = index;
        break;
      }
    }
    if (descriptorIndex == zc::none) { return zc::none; }
    const auto& slot = locked->descriptors[ZC_REQUIRE_NONNULL(descriptorIndex)];
    if (slot.registered == zc::none) { return zc::none; }
    const auto& kind = ZC_REQUIRE_NONNULL(slot.registered);
    if (admission == KeyAdmission::Validate && !kind.keyValidator(keyBytes.asPtr())) {
      failure = QueryRuntimeFailure::InvalidKeyEncoding;
      return zc::none;
    }

    return makeCanonicalKey(kind, zc::mv(keyBytes));
  }

  zc::Maybe<CanonicalQueryKey> makeKey(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                       QueryRuntimeFailure& failure) const {
    return makeKeyInternal(domain, zc::mv(keyBytes), failure, KeyAdmission::Trusted);
  }

  zc::Maybe<CanonicalQueryKey> makeValidatedKey(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                                QueryRuntimeFailure& failure) const {
    return makeKeyInternal(domain, zc::mv(keyBytes), failure, KeyAdmission::Validate);
  }

  zc::Maybe<QueryKindId> kindId(zc::StringPtr domain) const {
    auto locked = data.lockShared();
    for (const auto& slot : locked->descriptors) {
      if (slot.expected.domain == domain && slot.registered != zc::none) {
        return QueryKindId(slot.expected.ordinal);
      }
    }
    return zc::none;
  }

  RegisteredKind& kind(QueryKindId id) {
    auto locked = data.lockExclusive();
    ZC_IREQUIRE(id.value() < locked->descriptors.size(), "query kind id is out of range");
    auto& slot = locked->descriptors[id.value()];
    ZC_IREQUIRE(slot.registered != zc::none, "query descriptor slot is not registered");
    return ZC_REQUIRE_NONNULL(slot.registered);
  }

  void appendEvent(const SnapshotState& snapshot, const CanonicalQueryKey& key,
                   QueryEventKind kind) {
    auto locked = snapshot.runtime.lockExclusive();
    locked->events.add(QueryEvent(snapshot.revision, key.clone(), kind));
  }

  DetailedDemand demand(zc::Arc<SnapshotState> snapshot, CanonicalQueryKey&& key,
                        zc::Vector<CanonicalQueryKey>&& activeChain,
                        const CancellationSource::Token& cancellation, bool allowParallelGroups,
                        zc::Arc<const FinalSealAdmission> admission);

  DetailedDemand probeInput(zc::Arc<SnapshotState> snapshot, CanonicalQueryKey&& key,
                            const CancellationSource::Token& cancellation);

  zc::Vector<DetailedDemand> demandParallel(zc::Arc<SnapshotState> snapshot,
                                            zc::ArrayPtr<const CanonicalQueryKey> keys,
                                            zc::ArrayPtr<const CanonicalQueryKey> activeChain,
                                            zc::Arc<Flight> flight,
                                            zc::Arc<const FinalSealAdmission> admission);

  DetailedDemand execute(zc::Arc<SnapshotState> snapshot, RegisteredKind& kind,
                         CanonicalQueryKey&& key, zc::Maybe<Memo>&& prior,
                         zc::Vector<CanonicalQueryKey>&& activeChain, zc::Arc<Flight> flight,
                         bool allowParallelGroups, zc::Arc<const FinalSealAdmission> admission);
};

struct QueryContext::Impl final {
  Impl(QueryDatabase::Impl& database, zc::Arc<SnapshotState> snapshot,
       zc::Vector<CanonicalQueryKey>&& activeChain, zc::Arc<Flight> flight,
       bool allowParallelGroups, zc::Arc<const FinalSealAdmission>&& admission) noexcept
      : database(database),
        snapshot(zc::mv(snapshot)),
        activeChain(zc::mv(activeChain)),
        flight(zc::mv(flight)),
        allowParallelGroups(allowParallelGroups),
        admission(zc::mv(admission)) {
    database.retainBorrower();
  }
  ~Impl() noexcept(false) { database.releaseBorrower(); }

  QueryDatabase::Impl& database;
  zc::Arc<SnapshotState> snapshot;
  zc::Vector<CanonicalQueryKey> activeChain;
  zc::Arc<Flight> flight;
  bool allowParallelGroups;
  zc::Arc<const FinalSealAdmission> admission;
  zc::Vector<DependencyGroup> dependencies;
  zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>> capabilityDependencies;
  zc::Maybe<QueryRuntimeFailure> failure;
};

struct QuerySnapshot::Impl final {
  Impl(QueryDatabase::Impl& database, zc::Arc<SnapshotState>&& snapshot,
       zc::Arc<const FinalSealAdmission>&& admission) noexcept
      : database(database), snapshot(zc::mv(snapshot)), admission(zc::mv(admission)) {
    database.retainBorrower();
  }
  ~Impl() noexcept(false) { database.releaseBorrower(); }
  QueryDatabase::Impl& database;
  zc::Arc<SnapshotState> snapshot;
  zc::Arc<const FinalSealAdmission> admission;
};

struct InputTransaction::Impl final {
  Impl(QueryDatabase::Impl& database, QueryDatabaseIdentity&& databaseIdentity,
       DatabaseRevision baseRevision, zc::Vector<StagedInput>&& inputs) noexcept
      : database(database),
        databaseIdentity(zc::mv(databaseIdentity)),
        baseRevision(baseRevision),
        inputs(zc::mv(inputs)) {
    database.retainBorrower();
  }
  ~Impl() noexcept(false) { database.releaseBorrower(); }
  QueryDatabase::Impl& database;
  QueryDatabaseIdentity databaseIdentity;
  DatabaseRevision baseRevision;
  zc::Vector<StagedInput> inputs;
  bool closed = false;
};

QueryContext::QueryContext(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
QueryContext::QueryContext(QueryContext&&) noexcept = default;
QueryContext& QueryContext::operator=(QueryContext&&) noexcept = default;
QueryContext::~QueryContext() noexcept(false) = default;

bool QueryContext::isCancelled() const { return impl->flight->isCancelled(); }

const QueryDatabaseIdentity& QueryContext::databaseIdentity() const {
  return impl->snapshot->database;
}

DatabaseRevision QueryContext::snapshotRevision() const noexcept {
  return impl->snapshot->revision;
}

zc::Maybe<QueryRuntimeFailure> QueryContext::inheritedFinalAdmissionFailure() const {
  if (impl->admission == nullptr) { return QueryRuntimeFailure::FinalSealRequired; }
  auto locked = impl->database.data.lockShared();
  if (locked->finalSeal == nullptr || impl->admission != locked->finalSeal ||
      impl->admission->database != locked->database ||
      impl->admission->database != impl->snapshot->database ||
      impl->admission->revision != impl->snapshot->revision ||
      impl->admission->revision != locked->current->revision ||
      impl->admission->contextKey != locked->finalSeal->contextKey ||
      impl->admission->closureKind != locked->finalSeal->closureKind ||
      impl->admission->finalWitness.asPtr() != locked->finalSeal->finalWitness.asPtr()) {
    return QueryRuntimeFailure::FinalSealMismatch;
  }
  return zc::none;
}

FinalSnapshotClosureKind QueryContext::finalSnapshotClosureKind() const {
  ZC_IREQUIRE(impl->admission != nullptr, "final closure kind requires final admission");
  return impl->admission->closureKind;
}

const SemanticContextCapabilityResources& QueryContext::semanticContextResources() const {
  return impl->snapshot->capabilityArena->resources();
}

zc::Maybe<QueryRuntimeFailure> QueryContext::capabilityPublicationFailure() const {
  if (impl->failure != zc::none) { return ZC_REQUIRE_NONNULL(impl->failure); }
  if (impl->flight->isCancelled()) { return QueryRuntimeFailure::Cancelled; }
  return zc::none;
}

QueryRequestResult QueryContext::publishCapability(
    zc::Own<_query_detail::CapabilityMemoBuilderBase>&& builder) {
  ZC_IF_SOME(failure, capabilityPublicationFailure()) {
    return _query_detail::QueryRequestResultAccess::runtimeRejected(failure);
  }
  ZC_IREQUIRE(impl->activeChain.size() != 0, "capability publication has no active query key");
  auto memo = builder->publish(impl->snapshot->database.retain(), impl->activeChain.back().clone(),
                               impl->snapshot->revision, impl->snapshot->capabilityArena.addRef(),
                               zc::mv(impl->capabilityDependencies));
  return _query_detail::QueryRequestResultAccess::capabilityPublished(zc::mv(memo));
}

zc::Vector<DetailedDemand> QueryDatabase::Impl::demandParallel(
    zc::Arc<SnapshotState> snapshot, zc::ArrayPtr<const CanonicalQueryKey> keys,
    zc::ArrayPtr<const CanonicalQueryKey> activeChain, zc::Arc<Flight> flight,
    zc::Arc<const FinalSealAdmission> admission) {
  auto state = zc::arc<ParallelDemandState>(keys.size());
  for (size_t index = 0; index < keys.size(); ++index) {
    zc::Vector<CanonicalQueryKey> chain;
    for (const auto& active : activeChain) { chain.add(active.clone()); }
    auto cancellation = flight->cancellationToken();
    scheduler.enqueue([&database = *this, snapshot = snapshot.addRef(), key = keys[index].clone(),
                       chain = zc::mv(chain), cancellation = zc::mv(cancellation),
                       state = state.addRef(), admission = retainAdmission(admission),
                       index]() mutable {
      zc::Maybe<DetailedDemand> outcome;
      auto exception = zc::runCatchingExceptions([&]() {
        outcome = database.demand(snapshot.addRef(), zc::mv(key), zc::mv(chain), cancellation,
                                  false, zc::mv(admission));
      });
      if (exception != zc::none) {
        outcome = DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                     QueryRuntimeFailure::InvariantViolation),
                                 MemoMetadata());
      }
      auto locked = state->data.lockExclusive();
      locked->results[index] = zc::mv(ZC_REQUIRE_NONNULL(outcome));
      ++locked->completed;
    });
  }

  while (true) {
    auto locked = state->data.lockExclusive();
    if (locked->completed == keys.size()) { break; }
    locked.wait(
        [count = keys.size()](const ParallelDemandData& data) { return data.completed == count; },
        10 * zc::MILLISECONDS);
    locked.release();
    flight->isCancelled();
  }

  zc::Vector<DetailedDemand> results;
  auto locked = state->data.lockExclusive();
  for (auto& result : locked->results) { results.add(zc::mv(ZC_REQUIRE_NONNULL(result))); }
  return results;
}

QueryRequestResult QueryContext::getEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes) {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) {
    impl->failure = keyFailure;
    return _query_detail::QueryRequestResultAccess::runtimeRejected(keyFailure);
  }
  auto demandedKey = ZC_REQUIRE_NONNULL(key).clone();
  zc::Vector<CanonicalQueryKey> chain;
  for (const auto& active : impl->activeChain) { chain.add(active.clone()); }
  auto cancellation = impl->flight->cancellationToken();
  auto demand = impl->database.demand(impl->snapshot.addRef(), zc::mv(ZC_REQUIRE_NONNULL(key)),
                                      zc::mv(chain), cancellation, impl->allowParallelGroups,
                                      retainAdmission(impl->admission));
  if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(demand.result)) {
    impl->failure = _query_detail::QueryRequestResultAccess::runtimeFailure(demand.result);
    return zc::mv(demand.result);
  }
  if (_query_detail::QueryRequestResultAccess::isCapabilityPublished(demand.result)) {
    const auto& memo = _query_detail::QueryRequestResultAccess::capabilityMemo(demand.result);
    impl->dependencies.add(DependencyGroup::sequential(DependencyRecord::revisionLocalCapability(
        zc::mv(demandedKey), demand.metadata.changedAt(), demand.metadata.minimumDurability(),
        _query_detail::QueryRequestResultAccess::memoStableWitness(memo))));
    _query_detail::QueryRequestResultAccess::retainCapabilityDependency(
        impl->capabilityDependencies,
        _query_detail::QueryRequestResultAccess::retainCapabilityMemo(demand.result));
  } else if (_query_detail::QueryRequestResultAccess::isCapabilityRejected(demand.result)) {
    impl->dependencies.add(DependencyGroup::sequential(DependencyRecord(
        zc::mv(demandedKey), demand.metadata.changedAt(), demand.metadata.minimumDurability())));
    return zc::mv(demand.result);
  } else {
    impl->dependencies.add(DependencyGroup::sequential(DependencyRecord(
        zc::mv(demandedKey), demand.metadata.changedAt(), demand.metadata.minimumDurability())));
  }
  return zc::mv(demand.result);
}

QueryRequestResult QueryContext::probeInputEncoded(zc::StringPtr domain,
                                                   zc::Array<uint8_t>&& keyBytes) {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeValidatedKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) {
    impl->failure = keyFailure;
    return _query_detail::QueryRequestResultAccess::runtimeRejected(keyFailure);
  }
  auto demandedKey = ZC_REQUIRE_NONNULL(key).clone();
  auto cancellation = impl->flight->cancellationToken();
  auto demand = impl->database.probeInput(impl->snapshot.addRef(), zc::mv(ZC_REQUIRE_NONNULL(key)),
                                          cancellation);
  if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(demand.result)) {
    impl->failure = _query_detail::QueryRequestResultAccess::runtimeFailure(demand.result);
    return zc::mv(demand.result);
  }
  if (!_query_detail::QueryRequestResultAccess::isSemantic(demand.result)) {
    impl->failure = QueryRuntimeFailure::InvariantViolation;
    return _query_detail::QueryRequestResultAccess::runtimeRejected(
        QueryRuntimeFailure::InvariantViolation);
  }
  const auto observation =
      _query_detail::QueryRequestResultAccess::semanticValue(demand.result).kind() ==
              QueryValueKind::Absence
          ? InputProbeObservation::Absent
          : InputProbeObservation::Present;
  impl->dependencies.add(DependencyGroup::sequential(
      DependencyRecord(zc::mv(demandedKey), demand.metadata.changedAt(),
                       demand.metadata.minimumDurability(), observation)));
  return zc::mv(demand.result);
}

zc::Vector<QueryRequestResult> QueryContext::getParallelEncoded(
    zc::StringPtr domain, zc::Vector<zc::Array<uint8_t>>&& keyBytes) {
  if (!impl->allowParallelGroups) {
    impl->failure = QueryRuntimeFailure::InvariantViolation;
    zc::Vector<QueryRequestResult> failed;
    failed.add(_query_detail::QueryRequestResultAccess::runtimeRejected(
        QueryRuntimeFailure::InvariantViolation));
    return failed;
  }

  zc::Vector<CanonicalQueryKey> keys;
  zc::Vector<size_t> resultPositions;
  for (auto& bytes : keyBytes) {
    QueryRuntimeFailure keyFailure;
    auto key = impl->database.makeKey(domain, zc::mv(bytes), keyFailure);
    if (key == zc::none) {
      impl->failure = keyFailure;
      zc::Vector<QueryRequestResult> failed;
      failed.add(_query_detail::QueryRequestResultAccess::runtimeRejected(keyFailure));
      return failed;
    }
    resultPositions.add(resultPositions.size());
    keys.add(zc::mv(ZC_REQUIRE_NONNULL(key)));
  }
  for (size_t index = 1; index < keys.size(); ++index) {
    size_t position = index;
    while (position > 0 && keys[position] < keys[position - 1]) {
      auto temporary = zc::mv(keys[position]);
      keys[position] = zc::mv(keys[position - 1]);
      keys[position - 1] = zc::mv(temporary);
      const auto resultPosition = resultPositions[position];
      resultPositions[position] = resultPositions[position - 1];
      resultPositions[position - 1] = resultPosition;
      --position;
    }
  }

  auto demands = impl->database.demandParallel(impl->snapshot.addRef(), keys.asPtr(),
                                               impl->activeChain.asPtr(), impl->flight.addRef(),
                                               retainAdmission(impl->admission));
  if (demands.size() != keys.size()) {
    impl->failure = QueryRuntimeFailure::InvariantViolation;
    zc::Vector<QueryRequestResult> failed;
    failed.add(_query_detail::QueryRequestResultAccess::runtimeRejected(
        QueryRuntimeFailure::InvariantViolation));
    return failed;
  }
  zc::Vector<zc::Maybe<QueryRequestResult>> orderedResults;
  for (size_t index = 0; index < keys.size(); ++index) { orderedResults.add(zc::none); }
  zc::Vector<DependencyRecord> dependencies;
  bool failed = false;
  for (size_t index = 0; index < keys.size(); ++index) {
    auto& demand = demands[index];
    if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(demand.result)) {
      if (!failed) {
        impl->failure = _query_detail::QueryRequestResultAccess::runtimeFailure(demand.result);
      }
      failed = true;
      orderedResults[resultPositions[index]] = zc::mv(demand.result);
      continue;
    }
    if (_query_detail::QueryRequestResultAccess::isCapabilityPublished(demand.result)) {
      const auto& memo = _query_detail::QueryRequestResultAccess::capabilityMemo(demand.result);
      dependencies.add(DependencyRecord::revisionLocalCapability(
          keys[index].clone(), demand.metadata.changedAt(), demand.metadata.minimumDurability(),
          _query_detail::QueryRequestResultAccess::memoStableWitness(memo)));
      _query_detail::QueryRequestResultAccess::retainCapabilityDependency(
          impl->capabilityDependencies,
          _query_detail::QueryRequestResultAccess::retainCapabilityMemo(demand.result));
    } else if (_query_detail::QueryRequestResultAccess::isCapabilityRejected(demand.result)) {
      dependencies.add(DependencyRecord(keys[index].clone(), demand.metadata.changedAt(),
                                        demand.metadata.minimumDurability()));
      orderedResults[resultPositions[index]] = zc::mv(demand.result);
      continue;
    } else {
      dependencies.add(DependencyRecord(keys[index].clone(), demand.metadata.changedAt(),
                                        demand.metadata.minimumDurability()));
    }
    orderedResults[resultPositions[index]] = zc::mv(demand.result);
  }
  if (!failed) { impl->dependencies.add(DependencyGroup::parallel(zc::mv(dependencies))); }
  zc::Vector<QueryRequestResult> results;
  for (auto& result : orderedResults) { results.add(zc::mv(ZC_REQUIRE_NONNULL(result))); }
  return results;
}

QuerySnapshot::QuerySnapshot(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
QuerySnapshot::QuerySnapshot(QuerySnapshot&&) noexcept = default;
QuerySnapshot& QuerySnapshot::operator=(QuerySnapshot&&) noexcept = default;
QuerySnapshot::~QuerySnapshot() noexcept(false) = default;

DatabaseRevision QuerySnapshot::revision() const noexcept { return impl->snapshot->revision; }

const QueryDatabaseIdentity& QuerySnapshot::databaseIdentity() const {
  return impl->snapshot->database;
}

QueryRequestResult QuerySnapshot::getEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                             const CancellationSource::Token& cancellation) const {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) {
    return _query_detail::QueryRequestResultAccess::runtimeRejected(keyFailure);
  }
  zc::Vector<CanonicalQueryKey> chain;
  auto demand =
      impl->database.demand(impl->snapshot.addRef(), zc::mv(ZC_REQUIRE_NONNULL(key)), zc::mv(chain),
                            cancellation, true, retainAdmission(impl->admission));
  return zc::mv(demand.result);
}

QueryRequestResult QuerySnapshot::probeInputEncoded(
    zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
    const CancellationSource::Token& cancellation) const {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeValidatedKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) {
    return _query_detail::QueryRequestResultAccess::runtimeRejected(keyFailure);
  }
  auto demand = impl->database.probeInput(impl->snapshot.addRef(), zc::mv(ZC_REQUIRE_NONNULL(key)),
                                          cancellation);
  return zc::mv(demand.result);
}

zc::Maybe<MemoMetadata> QuerySnapshot::metadataEncoded(zc::StringPtr domain,
                                                       zc::Array<uint8_t>&& keyBytes) const {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return zc::none; }
  auto locked = impl->snapshot->runtime.lockShared();
  bool collision = false;
  ZC_IF_SOME(index, exactMemoIndex(*locked, ZC_REQUIRE_NONNULL(key), collision)) {
    return locked->memos[index].metadata;
  }
  ZC_IF_SOME(index, exactInputIndex(*locked, ZC_REQUIRE_NONNULL(key), collision)) {
    const auto& input = locked->inputs[index];
    return MemoMetadata(impl->snapshot->revision, input.changedAt, input.durability);
  }
  return zc::none;
}

zc::Vector<DependencyGroup> QuerySnapshot::dependenciesEncoded(
    zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes) const {
  zc::Vector<DependencyGroup> result;
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return result; }
  auto locked = impl->snapshot->runtime.lockShared();
  bool collision = false;
  ZC_IF_SOME(index, exactMemoIndex(*locked, ZC_REQUIRE_NONNULL(key), collision)) {
    for (const auto& group : locked->memos[index].dependencies) { result.add(group.clone()); }
  }
  return result;
}

bool QuerySnapshot::evictValueEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes) const {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return false; }
  const auto& canonicalKey = ZC_REQUIRE_NONNULL(key);
  const auto& descriptor = impl->database.kind(canonicalKey.kind());
  if (descriptor.descriptor.kind == QueryDescriptorKind::Input ||
      descriptor.descriptor.retention != RetentionClass::Evictable) {
    return false;
  }

  auto locked = impl->snapshot->runtime.lockExclusive();
  bool collision = false;
  if (exactFlightIndex(*locked, canonicalKey, collision) != zc::none || collision) { return false; }
  ZC_IF_SOME(index, exactMemoIndex(*locked, canonicalKey, collision)) {
    if (locked->memos[index].value == zc::none) { return false; }
    locked->memos[index].value = zc::none;
    locked->events.add(
        QueryEvent(impl->snapshot->revision, canonicalKey.clone(), QueryEventKind::ValueEvicted));
    return true;
  }
  return false;
}

bool QuerySnapshot::hasRetainedValueEncoded(zc::StringPtr domain,
                                            zc::Array<uint8_t>&& keyBytes) const {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return false; }
  const auto& canonicalKey = ZC_REQUIRE_NONNULL(key);
  auto locked = impl->snapshot->runtime.lockShared();
  bool collision = false;
  ZC_IF_SOME(index, exactMemoIndex(*locked, canonicalKey, collision)) {
    return locked->memos[index].value != zc::none || locked->memos[index].capability != nullptr;
  }
  if (collision) { return false; }
  ZC_IF_SOME(index, exactInputIndex(*locked, canonicalKey, collision)) {
    static_cast<void>(index);
    return true;
  }
  return false;
}

zc::Maybe<QueryKeyFingerprint> QuerySnapshot::keyFingerprintEncoded(
    zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes) const {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return zc::none; }
  return ZC_REQUIRE_NONNULL(key).fingerprint();
}

zc::Vector<QueryEvent> QuerySnapshot::events() const {
  zc::Vector<QueryEvent> result;
  auto locked = impl->snapshot->runtime.lockShared();
  for (const auto& event : locked->events) { result.add(event.clone()); }
  for (size_t index = 1; index < result.size(); ++index) {
    size_t position = index;
    while (position > 0 && (result[position].key() < result[position - 1].key() ||
                            (result[position].key() == result[position - 1].key() &&
                             static_cast<uint8_t>(result[position].kind()) <
                                 static_cast<uint8_t>(result[position - 1].kind())))) {
      auto temporary = zc::mv(result[position]);
      result[position] = zc::mv(result[position - 1]);
      result[position - 1] = zc::mv(temporary);
      --position;
    }
  }
  return result;
}

InputTransaction::InputTransaction(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
InputTransaction::InputTransaction(InputTransaction&&) noexcept = default;
InputTransaction& InputTransaction::operator=(InputTransaction&& other) noexcept(false) {
  if (this == &other) { return *this; }
  if (impl.get() != nullptr && !impl->closed) { abandon(); }
  impl = zc::mv(other.impl);
  return *this;
}
InputTransaction::~InputTransaction() noexcept(false) {
  if (impl.get() != nullptr && !impl->closed) { abandon(); }
}

InputMutationResult InputTransaction::stageEncoded(zc::StringPtr domain,
                                                   zc::Array<uint8_t>&& keyBytes,
                                                   QueryValue&& value) {
  {
    auto database = impl->database.data.lockShared();
    if (database->finalSeal != nullptr) {
      return InputMutationResult::rejected(InputTransactionFailure::InputMutationAfterFinalSeal);
    }
  }
  if (impl->closed) {
    return InputMutationResult::rejected(InputTransactionFailure::TransactionClosed);
  }
  auto descriptorKind = impl->database.kindId(domain);
  if (descriptorKind == zc::none) {
    return InputMutationResult::rejected(InputTransactionFailure::UnknownDescriptor);
  }
  RegisteredKind& kind = impl->database.kind(ZC_REQUIRE_NONNULL(descriptorKind));
  if (kind.descriptor.kind != QueryDescriptorKind::Input) {
    return InputMutationResult::rejected(InputTransactionFailure::DescriptorKindMismatch);
  }
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeValidatedKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) {
    return InputMutationResult::rejected(InputTransactionFailure::InvalidKeyEncoding);
  }

  for (auto& input : impl->inputs) {
    if (!keyFingerprintMatches(input.current.key, ZC_REQUIRE_NONNULL(key))) { continue; }
    if (input.current.key != ZC_REQUIRE_NONNULL(key)) {
      return InputMutationResult::rejected(InputTransactionFailure::FingerprintCollision);
    }
    if (input.operated) {
      return InputMutationResult::rejected(InputTransactionFailure::DuplicateInputOperation);
    }
    if (input.current.durability == Durability::Frozen && input.existedAtBase &&
        input.baseValue != value) {
      return InputMutationResult::rejected(InputTransactionFailure::FrozenInputMutation);
    }
    input.current.value = zc::mv(value);
    input.present = true;
    input.operated = true;
    input.refreshChanged();
    return InputMutationResult::applied();
  }

  auto base = QueryValue::absence();
  impl->inputs.add(StagedInput(InputEntry(zc::mv(ZC_REQUIRE_NONNULL(key)), zc::mv(value),
                                          DatabaseRevision(), kind.descriptor.durability),
                               zc::mv(base), true, false, true, true));
  return InputMutationResult::applied();
}

InputMutationResult InputTransaction::eraseEncoded(zc::StringPtr domain,
                                                   zc::Array<uint8_t>&& keyBytes) {
  {
    auto database = impl->database.data.lockShared();
    if (database->finalSeal != nullptr) {
      return InputMutationResult::rejected(InputTransactionFailure::InputMutationAfterFinalSeal);
    }
  }
  if (impl->closed) {
    return InputMutationResult::rejected(InputTransactionFailure::TransactionClosed);
  }
  auto descriptorKind = impl->database.kindId(domain);
  if (descriptorKind == zc::none) {
    return InputMutationResult::rejected(InputTransactionFailure::UnknownDescriptor);
  }
  RegisteredKind& kind = impl->database.kind(ZC_REQUIRE_NONNULL(descriptorKind));
  if (kind.descriptor.kind != QueryDescriptorKind::Input) {
    return InputMutationResult::rejected(InputTransactionFailure::DescriptorKindMismatch);
  }
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeValidatedKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) {
    return InputMutationResult::rejected(InputTransactionFailure::InvalidKeyEncoding);
  }

  for (auto& input : impl->inputs) {
    if (!keyFingerprintMatches(input.current.key, ZC_REQUIRE_NONNULL(key))) { continue; }
    if (input.current.key != ZC_REQUIRE_NONNULL(key)) {
      return InputMutationResult::rejected(InputTransactionFailure::FingerprintCollision);
    }
    if (input.operated) {
      return InputMutationResult::rejected(InputTransactionFailure::DuplicateInputOperation);
    }
    if (kind.descriptor.durability == Durability::Frozen) {
      return InputMutationResult::rejected(InputTransactionFailure::FrozenInputMutation);
    }
    if (!input.existedAtBase) {
      return InputMutationResult::rejected(InputTransactionFailure::MissingInputForErase);
    }
    input.present = false;
    input.operated = true;
    input.refreshChanged();
    return InputMutationResult::applied();
  }
  if (kind.descriptor.durability == Durability::Frozen) {
    return InputMutationResult::rejected(InputTransactionFailure::FrozenInputMutation);
  }
  return InputMutationResult::rejected(InputTransactionFailure::MissingInputForErase);
}

InputCommitResult InputTransaction::commit() {
  auto database = impl->database.data.lockExclusive();
  if (database->finalSeal != nullptr) {
    impl->closed = true;
    return InputCommitResult::rejected(InputTransactionFailure::InputMutationAfterFinalSeal);
  }
  if (impl->closed) {
    return InputCommitResult::rejected(InputTransactionFailure::TransactionClosed);
  }
  if (database->current->revision != impl->baseRevision || !database->transactionOpen) {
    database->transactionOpen = false;
    impl->closed = true;
    return InputCommitResult::rejected(InputTransactionFailure::StaleBaseRevision);
  }
  if (impl->baseRevision.value() == UINT64_MAX) {
    database->transactionOpen = false;
    impl->closed = true;
    return InputCommitResult::rejected(InputTransactionFailure::RevisionExhausted);
  }
  const DatabaseRevision nextRevision = impl->baseRevision.next();
  DatabaseRevision nextLastChanged[4];
  for (size_t level = 0; level < 4; ++level) {
    nextLastChanged[level] = database->current->lastChanged[level];
  }
  for (const auto& input : impl->inputs) {
    if (!input.changed) { continue; }
    const size_t changedLevel = static_cast<size_t>(input.current.durability);
    for (size_t level = 0; level <= changedLevel; ++level) {
      nextLastChanged[level] = nextRevision;
    }
  }
  auto next =
      zc::arc<SnapshotState>(database->database.retain(), nextRevision,
                             zc::arrayPtr(nextLastChanged), database->capabilityArena.addRef());

  {
    auto nextRuntime = next->runtime.lockExclusive();
    for (auto& input : impl->inputs) {
      if (input.changed) { input.current.changedAt = nextRevision; }
      if (!input.present) { continue; }
      nextRuntime->inputs.add(input.current.clone());
    }
    auto priorRuntime = database->current->runtime.lockShared();
    for (const auto& memo : priorRuntime->memos) {
      const auto& slot = database->descriptors[memo.key.kind().value()];
      ZC_IREQUIRE(slot.registered != zc::none, "memo references an unregistered descriptor slot");
      const auto& descriptor = ZC_REQUIRE_NONNULL(slot.registered);
      if (descriptor.capabilityEvaluator != zc::none) { continue; }
      nextRuntime->memos.add(memo.clone());
    }
  }

  database->current = zc::mv(next);
  database->transactionOpen = false;
  impl->closed = true;
  return InputCommitResult::committed(nextRevision);
}

void InputTransaction::abandon() {
  if (impl->closed) { return; }
  auto database = impl->database.data.lockExclusive();
  database->transactionOpen = false;
  impl->closed = true;
}

QueryDatabase::QueryDatabase(basic::ThreadPool& scheduler,
                             QueryDescriptorInventoryRef descriptorInventory)
    : QueryDatabase(scheduler, descriptorInventory, zc::arc<SemanticContextCapabilityArena>()) {}
QueryDatabase::QueryDatabase(basic::ThreadPool& scheduler,
                             QueryDescriptorInventoryRef descriptorInventory,
                             zc::Arc<SemanticContextCapabilityArena>&& capabilityArena)
    : impl(zc::heap<Impl>(scheduler, descriptorInventory, zc::mv(capabilityArena))) {}
QueryDatabase::~QueryDatabase() noexcept(false) {
  if (impl.get() != nullptr) { impl->requireNoBorrowers(); }
}
QueryDatabase::QueryDatabase(QueryDatabase&&) noexcept = default;
QueryDatabase& QueryDatabase::operator=(QueryDatabase&& other) noexcept(false) {
  if (this == &other) { return *this; }
  if (impl.get() != nullptr) { impl->requireNoBorrowers(); }
  impl = zc::mv(other.impl);
  return *this;
}

DescriptorRegistrationResult QueryDatabase::installDescriptor(
    zc::StringPtr inventoryIdentity, const QueryDescriptorInventoryRow& descriptor,
    ErasedKeyValidator&& keyValidator, zc::Maybe<ErasedProvider>&& provider,
    zc::Maybe<ErasedVerifier>&& verifier,
    zc::Maybe<ErasedCapabilityEvaluator>&& capabilityEvaluator) {
  auto locked = impl->data.lockExclusive();
  zc::Maybe<size_t> rowIndex;
  for (size_t index = 0; index < locked->descriptors.size(); ++index) {
    if (locked->descriptors[index].expected.descriptorType == descriptor.descriptorType) {
      rowIndex = index;
      break;
    }
  }
  if (rowIndex == zc::none) {
    return DescriptorRegistrationResult::rejected(
        DescriptorRegistrationFailure::DescriptorAbsentFromInventory);
  }
  if (inventoryIdentity != locked->descriptorInventory.identity()) {
    return DescriptorRegistrationResult::rejected(DescriptorRegistrationFailure::InventoryMismatch);
  }
  const auto& expected = locked->descriptors[ZC_REQUIRE_NONNULL(rowIndex)].expected;
  const bool metadataMatches =
      descriptorRowsEqual(expected, descriptor) && descriptorMetadataIsValid(descriptor);
  const bool callbackShapeMatches =
      (descriptor.kind == QueryDescriptorKind::Input && provider == zc::none &&
       verifier == zc::none && capabilityEvaluator == zc::none) ||
      (descriptor.kind == QueryDescriptorKind::Semantic && provider != zc::none &&
       verifier != zc::none && capabilityEvaluator == zc::none) ||
      (descriptor.kind == QueryDescriptorKind::RevisionLocalCapability && provider == zc::none &&
       verifier == zc::none && capabilityEvaluator != zc::none &&
       descriptor.reuse == ReuseClass::RevisionLocal &&
       descriptor.retention == RetentionClass::Retained);
  if (!metadataMatches || !callbackShapeMatches) {
    return DescriptorRegistrationResult::rejected(DescriptorRegistrationFailure::MetadataMismatch);
  }
  ZC_IREQUIRE(!locked->registrySealed,
              "query descriptor registration attempted after registry publication");
  if (descriptor.kind == QueryDescriptorKind::RevisionLocalCapability) {
    ZC_IREQUIRE(locked->capabilityArena->hasResources(),
                "capability descriptor registration requires semantic resources");
  }
  auto& slot = locked->descriptors[ZC_REQUIRE_NONNULL(rowIndex)];
  if (slot.registered != zc::none) {
    if (ZC_REQUIRE_NONNULL(slot.registered).descriptor.descriptorType ==
        descriptor.descriptorType) {
      return DescriptorRegistrationResult::rejected(
          DescriptorRegistrationFailure::SlotAlreadyRegistered);
    }
    return DescriptorRegistrationResult::rejected(DescriptorRegistrationFailure::SlotCollision);
  }
  slot.registered = RegisteredKind(descriptor, zc::mv(keyValidator), zc::mv(provider),
                                   zc::mv(verifier), zc::mv(capabilityEvaluator));
  return DescriptorRegistrationResult::registered(QueryKindId(descriptor.ordinal));
}

QuerySnapshot QueryDatabase::snapshot() {
  auto locked = impl->data.lockExclusive();
  locked->registrySealed = true;
  zc::Arc<const FinalSealAdmission> noAdmission;
  return QuerySnapshot(
      zc::heap<QuerySnapshot::Impl>(*impl, locked->current.addRef(), zc::mv(noAdmission)));
}

InputTransactionOpenResult QueryDatabase::beginInputTransaction(
    DatabaseRevision expectedPreviousRevision) {
  auto locked = impl->data.lockExclusive();
  if (locked->finalSeal != nullptr) {
    return InputTransactionOpenResult::rejected(
        InputTransactionFailure::InputMutationAfterFinalSeal);
  }
  if (locked->transactionOpen) {
    return InputTransactionOpenResult::rejected(InputTransactionFailure::TransactionAlreadyOpen);
  }
  if (locked->current->revision != expectedPreviousRevision) {
    return InputTransactionOpenResult::rejected(InputTransactionFailure::StaleBaseRevision);
  }
  locked->registrySealed = true;
  locked->transactionOpen = true;
  zc::Vector<StagedInput> inputs;
  auto runtime = locked->current->runtime.lockShared();
  for (const auto& input : runtime->inputs) {
    inputs.add(StagedInput(input.clone(), input.value.clone(), false, true, true, false));
  }
  return InputTransactionOpenResult::opened(InputTransaction(zc::heap<InputTransaction::Impl>(
      *impl, locked->database.retain(), locked->current->revision, zc::mv(inputs))));
}

void QueryDatabase::armFinalSealPhaseTwoGateForTest() {
  auto gate = impl->finalSealPhaseTwoGate.lockExclusive();
  ZC_IREQUIRE(!gate->armed && !gate->claimed, "final-seal phase-two test gate is already active");
  gate->armed = true;
  gate->entered = false;
  gate->released = false;
}

void QueryDatabase::waitForFinalSealPhaseTwoGateForTest() {
  auto gate = impl->finalSealPhaseTwoGate.lockExclusive();
  gate.wait([](const FinalSealPhaseTwoGateState& state) { return state.entered; });
}

void QueryDatabase::releaseFinalSealPhaseTwoGateForTest() {
  auto gate = impl->finalSealPhaseTwoGate.lockExclusive();
  ZC_IREQUIRE(gate->entered && !gate->released,
              "final-seal phase-two test gate has not been entered");
  gate->released = true;
}

void QueryDatabase::pauseAtFinalSealPhaseTwoGate() {
  auto gate = impl->finalSealPhaseTwoGate.lockExclusive();
  if (!gate->armed || gate->claimed) { return; }
  gate->claimed = true;
  gate->entered = true;
  gate.wait([](const FinalSealPhaseTwoGateState& state) { return state.released; });
  gate->armed = false;
  gate->claimed = false;
}

_query_detail::FinalSealPreparationResult QueryDatabase::prepareFinalSeal(
    const QuerySnapshot& snapshot, zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes) {
  auto locked = impl->data.lockExclusive();
  if (locked->finalSeal != nullptr) {
    return _query_detail::FinalSealPreparationResult::rejected(
        InputTransactionFailure::FinalSealAlreadyPublished);
  }
  if (locked->transactionOpen) {
    return _query_detail::FinalSealPreparationResult::rejected(
        InputTransactionFailure::OpenTransactionDuringFinalSeal);
  }
  if (snapshot.impl->snapshot->database != locked->database) {
    return _query_detail::FinalSealPreparationResult::rejected(
        InputTransactionFailure::ForeignSnapshot);
  }
  if (snapshot.impl->snapshot->revision != locked->current->revision) {
    return _query_detail::FinalSealPreparationResult::rejected(
        InputTransactionFailure::StaleSnapshot);
  }

  zc::Maybe<size_t> descriptorIndex;
  for (size_t index = 0; index < locked->descriptors.size(); ++index) {
    if (locked->descriptors[index].expected.domain == domain &&
        locked->descriptors[index].registered != zc::none) {
      descriptorIndex = index;
      break;
    }
  }
  if (descriptorIndex == zc::none) {
    return _query_detail::FinalSealPreparationResult::rejected(
        InputTransactionFailure::UnknownDescriptor);
  }
  const auto& descriptor =
      ZC_REQUIRE_NONNULL(locked->descriptors[ZC_REQUIRE_NONNULL(descriptorIndex)].registered);
  if (descriptor.descriptor.kind != QueryDescriptorKind::Input) {
    return _query_detail::FinalSealPreparationResult::rejected(
        InputTransactionFailure::DescriptorKindMismatch);
  }
  if (keyBytes.size() > UINT32_MAX || !descriptor.keyValidator(keyBytes.asPtr())) {
    return _query_detail::FinalSealPreparationResult::rejected(
        InputTransactionFailure::InvalidFinalSealAuthority);
  }

  auto contextKey = impl->makeCanonicalKey(descriptor, zc::mv(keyBytes));
  zc::Arc<const _query_detail::FinalSealPreparationState> retainedState =
      zc::arc<_query_detail::FinalSealPreparationState>(snapshot.impl->snapshot.addRef(),
                                                        descriptor.descriptor);
  return _query_detail::FinalSealPreparationResult::prepared(
      _query_detail::FinalSealPreparation(locked->database.retain(), locked->current->revision,
                                          zc::mv(contextKey), zc::mv(retainedState)));
}

InputTransactionFailure QueryDatabase::rejectFinalSeal(
    _query_detail::FinalSealPreparation&& preparation) {
  auto locked = impl->data.lockExclusive();
  if (locked->finalSeal != nullptr) { return InputTransactionFailure::FinalSealAlreadyPublished; }
  if (locked->transactionOpen) { return InputTransactionFailure::OpenTransactionDuringFinalSeal; }
  if (preparation.databaseField != locked->database) {
    return InputTransactionFailure::ForeignSnapshot;
  }
  if (preparation.revisionField != locked->current->revision) {
    return InputTransactionFailure::StaleSnapshot;
  }
  ZC_IREQUIRE(preparation.stateField != nullptr,
              "final seal preparation lost retained phase-one state");
  const auto& phaseOne = *preparation.stateField.get();
  if (phaseOne.snapshotField->database != preparation.databaseField) {
    return InputTransactionFailure::ForeignSnapshot;
  }
  if (phaseOne.snapshotField != locked->current ||
      phaseOne.snapshotField->revision != preparation.revisionField) {
    return InputTransactionFailure::StaleSnapshot;
  }
  const auto kind = preparation.contextKeyField.kind();
  if (kind.value() >= locked->descriptors.size() ||
      locked->descriptors[kind.value()].registered == zc::none) {
    return InputTransactionFailure::UnknownDescriptor;
  }
  const auto& descriptor = ZC_REQUIRE_NONNULL(locked->descriptors[kind.value()].registered);
  if (!descriptorRowsEqual(descriptor.descriptor, phaseOne.descriptorField)) {
    return InputTransactionFailure::UnknownDescriptor;
  }
  if (descriptor.descriptor.kind != QueryDescriptorKind::Input) {
    return InputTransactionFailure::DescriptorKindMismatch;
  }
  if (!descriptor.keyValidator(preparation.contextKeyField.canonicalBytes())) {
    return InputTransactionFailure::InvalidFinalSealAuthority;
  }
  auto reconstructed = impl->makeCanonicalKey(
      descriptor, zc::heapArray<uint8_t>(preparation.contextKeyField.canonicalBytes()));
  if (reconstructed != preparation.contextKeyField) {
    return InputTransactionFailure::InvalidFinalSealAuthority;
  }
  return InputTransactionFailure::InvalidFinalSealAuthority;
}

zc::Maybe<InputTransactionFailure> QueryDatabase::publishFinalSeal(
    _query_detail::FinalSealPreparation&& preparation,
    _query_detail::VerifiedFinalSealAuthority&& authority,
    zc::ArrayPtr<const uint8_t> suppliedWitness) {
  auto locked = impl->data.lockExclusive();
  if (locked->finalSeal != nullptr) { return InputTransactionFailure::FinalSealAlreadyPublished; }
  if (locked->transactionOpen) { return InputTransactionFailure::OpenTransactionDuringFinalSeal; }
  if (preparation.databaseField != locked->database ||
      authority.databaseField != locked->database ||
      authority.databaseField != preparation.databaseField) {
    return InputTransactionFailure::ForeignSnapshot;
  }
  if (preparation.revisionField != locked->current->revision ||
      authority.revisionField != locked->current->revision ||
      authority.revisionField != preparation.revisionField) {
    return InputTransactionFailure::StaleSnapshot;
  }
  ZC_IREQUIRE(preparation.stateField != nullptr,
              "verified final seal lost retained phase-one state");
  const auto& phaseOne = *preparation.stateField.get();
  if (phaseOne.snapshotField->database != preparation.databaseField) {
    return InputTransactionFailure::ForeignSnapshot;
  }
  if (phaseOne.snapshotField != locked->current ||
      phaseOne.snapshotField->revision != preparation.revisionField) {
    return InputTransactionFailure::StaleSnapshot;
  }
  const auto kind = authority.contextKeyField.kind();
  if (kind.value() >= locked->descriptors.size() ||
      locked->descriptors[kind.value()].registered == zc::none) {
    return InputTransactionFailure::UnknownDescriptor;
  }
  const auto& descriptor = ZC_REQUIRE_NONNULL(locked->descriptors[kind.value()].registered);
  if (!descriptorRowsEqual(descriptor.descriptor, phaseOne.descriptorField)) {
    return InputTransactionFailure::UnknownDescriptor;
  }
  if (descriptor.descriptor.kind != QueryDescriptorKind::Input) {
    return InputTransactionFailure::DescriptorKindMismatch;
  }
  if (!descriptor.keyValidator(authority.contextKeyField.canonicalBytes())) {
    return InputTransactionFailure::InvalidFinalSealAuthority;
  }
  auto reconstructed = impl->makeCanonicalKey(
      descriptor, zc::heapArray<uint8_t>(authority.contextKeyField.canonicalBytes()));
  if (preparation.contextKeyField != authority.contextKeyField ||
      reconstructed != authority.contextKeyField || authority.finalWitnessField.size() == 0 ||
      (authority.closureKindField != FinalSnapshotClosureKind::Success &&
       authority.closureKindField != FinalSnapshotClosureKind::Failure) ||
      authority.finalWitnessField.asPtr() != suppliedWitness) {
    return InputTransactionFailure::InvalidFinalSealAuthority;
  }

  locked->registrySealed = true;
  locked->finalSeal = zc::arc<FinalSealAdmission>(
      locked->database.retain(), locked->current->revision, authority.contextKeyField.clone(),
      authority.closureKindField, zc::mv(authority.finalWitnessField));
  return zc::none;
}

zc::Maybe<QueryRuntimeFailure> QueryDatabase::validateSnapshotAdmission(
    QuerySnapshot& snapshot, const QueryDatabaseIdentity& sealDatabase,
    DatabaseRevision sealRevision, zc::StringPtr descriptorDomain,
    zc::Array<uint8_t>&& contextKeyBytes, FinalSnapshotClosureKind closureKind,
    zc::ArrayPtr<const uint8_t> finalWitness) {
  auto locked = impl->data.lockShared();
  if (locked->finalSeal == nullptr) { return QueryRuntimeFailure::FinalSealRequired; }
  if (snapshot.impl->snapshot->database != locked->database ||
      snapshot.impl->snapshot->revision != locked->current->revision ||
      sealDatabase != locked->database || sealRevision != locked->current->revision ||
      snapshot.impl->admission != nullptr) {
    return QueryRuntimeFailure::FinalSealMismatch;
  }

  zc::Maybe<size_t> descriptorIndex;
  for (size_t index = 0; index < locked->descriptors.size(); ++index) {
    if (locked->descriptors[index].expected.domain == descriptorDomain &&
        locked->descriptors[index].registered != zc::none) {
      descriptorIndex = index;
      break;
    }
  }
  if (descriptorIndex == zc::none) { return QueryRuntimeFailure::FinalSealMismatch; }
  const auto& descriptor =
      ZC_REQUIRE_NONNULL(locked->descriptors[ZC_REQUIRE_NONNULL(descriptorIndex)].registered);
  if (descriptor.descriptor.kind != QueryDescriptorKind::Input ||
      !descriptor.keyValidator(contextKeyBytes.asPtr())) {
    return QueryRuntimeFailure::FinalSealMismatch;
  }
  auto contextKey = impl->makeCanonicalKey(descriptor, zc::mv(contextKeyBytes));
  if (contextKey != locked->finalSeal->contextKey ||
      closureKind != locked->finalSeal->closureKind ||
      finalWitness != locked->finalSeal->finalWitness.asPtr()) {
    return QueryRuntimeFailure::FinalSealMismatch;
  }
  snapshot.impl->admission = locked->finalSeal.addRef();
  return zc::none;
}

DetailedDemand QueryDatabase::Impl::demand(zc::Arc<SnapshotState> snapshot, CanonicalQueryKey&& key,
                                           zc::Vector<CanonicalQueryKey>&& activeChain,
                                           const CancellationSource::Token& cancellation,
                                           bool allowParallelGroups,
                                           zc::Arc<const FinalSealAdmission> admission) {
  RegisteredKind& descriptor = kind(key.kind());
  if (descriptor.capabilityEvaluator != zc::none &&
      descriptor.descriptor.admission == CapabilityAdmission::FinalSealedSnapshot) {
    if (admission == nullptr) {
      return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                QueryRuntimeFailure::FinalSealRequired),
                            MemoMetadata());
    }
    auto locked = data.lockShared();
    if (locked->finalSeal == nullptr || admission != locked->finalSeal ||
        admission->database != locked->database || admission->database != snapshot->database ||
        admission->revision != snapshot->revision ||
        admission->revision != locked->current->revision ||
        admission->contextKey != locked->finalSeal->contextKey ||
        admission->finalWitness.asPtr() != locked->finalSeal->finalWitness.asPtr()) {
      return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                QueryRuntimeFailure::FinalSealMismatch),
                            MemoMetadata());
    }
  }
  if (cancellation.isCancelled()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(
        _query_detail::QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::Cancelled),
        MemoMetadata());
  }
  if (containsKey(activeChain.asPtr(), key)) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cycle);
    return DetailedDemand(
        _query_detail::QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::Cycle),
        MemoMetadata());
  }

  if (descriptor.descriptor.kind == QueryDescriptorKind::Input) {
    auto locked = snapshot->runtime.lockShared();
    bool collision = false;
    ZC_IF_SOME(index, exactInputIndex(*locked, key, collision)) {
      const auto& input = locked->inputs[index];
      return DetailedDemand(_query_detail::QueryRequestResultAccess::semantic(input.value.clone()),
                            MemoMetadata(snapshot->revision, input.changedAt, input.durability));
    }
    return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                              collision ? QueryRuntimeFailure::FingerprintCollision
                                        : QueryRuntimeFailure::MissingInput),
                          MemoMetadata());
  }

  zc::Maybe<Memo> prior;
  zc::Arc<Flight> flight;
  bool evaluate = false;
  bool waitEdgeAdded = false;
  CanonicalQueryKey parent = key.clone();
  if (activeChain.size() != 0) { parent = activeChain.back().clone(); }
  {
    auto locked = snapshot->runtime.lockExclusive();
    bool collision = false;
    ZC_IF_SOME(index, exactMemoIndex(*locked, key, collision)) {
      auto& memo = locked->memos[index];
      if (memo.metadata.verifiedAt() == snapshot->revision &&
          (memo.value != zc::none || memo.capability != nullptr)) {
        QueryRequestResult result =
            memo.capability != nullptr
                ? _query_detail::QueryRequestResultAccess::capabilityPublished(
                      memo.capability.addRef())
                : _query_detail::QueryRequestResultAccess::semantic(
                      ZC_REQUIRE_NONNULL(memo.value).clone());
        const auto metadata = memo.metadata;
        locked->events.add(
            QueryEvent(snapshot->revision, key.clone(), QueryEventKind::GreenReused));
        return DetailedDemand(zc::mv(result), metadata);
      }
      prior = memo.clone();
    }
    if (collision) {
      return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                QueryRuntimeFailure::FingerprintCollision),
                            MemoMetadata());
    }
    ZC_IF_SOME(index, exactFlightIndex(*locked, key, collision)) {
      flight = locked->flights[index].addRef();
      flight->addRequester(cancellation);
      if (activeChain.size() != 0) {
        locked->waitEdges.add(WaitEdge(parent.clone(), key.clone()));
        waitEdgeAdded = true;
        zc::Vector<CanonicalQueryKey> visited;
        if (pathExists(*locked, key, parent, visited)) {
          removeWaitEdge(*locked, parent, key);
          locked->events.add(QueryEvent(snapshot->revision, key.clone(), QueryEventKind::Cycle));
          return DetailedDemand(
              _query_detail::QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::Cycle),
              MemoMetadata());
        }
      }
      locked->events.add(
          QueryEvent(snapshot->revision, key.clone(), QueryEventKind::SingleFlightJoined));
    } else {
      if (collision) {
        return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                  QueryRuntimeFailure::FingerprintCollision),
                              MemoMetadata());
      }
      flight = zc::arc<Flight>(key.clone(), cancellation.clone());
      locked->flights.add(flight.addRef());
      evaluate = true;
    }
  }

  if (!evaluate) {
    while (true) {
      if (cancellation.isCancelled()) {
        if (waitEdgeAdded) {
          auto locked = snapshot->runtime.lockExclusive();
          removeWaitEdge(*locked, parent, key);
        }
        appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
        return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                  QueryRuntimeFailure::Cancelled),
                              MemoMetadata());
      }
      auto state = flight->data.lockExclusive();
      state.wait([](const FlightData& data) { return data.result != zc::none; },
                 10 * zc::MILLISECONDS);
      ZC_IF_SOME(result, state->result) {
        auto clonedResult = _query_detail::QueryRequestResultAccess::retain(result);
        if (waitEdgeAdded) {
          state.release();
          auto locked = snapshot->runtime.lockExclusive();
          removeWaitEdge(*locked, parent, key);
        }
        if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(clonedResult)) {
          return DetailedDemand(zc::mv(clonedResult), MemoMetadata());
        }
        auto locked = snapshot->runtime.lockShared();
        bool collision = false;
        ZC_IF_SOME(index, exactMemoIndex(*locked, key, collision)) {
          return DetailedDemand(zc::mv(clonedResult), locked->memos[index].metadata);
        }
        return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                  QueryRuntimeFailure::InvariantViolation),
                              MemoMetadata());
      }
    }
  }

  auto outcome =
      execute(snapshot.addRef(), descriptor, key.clone(), zc::mv(prior), zc::mv(activeChain),
              flight.addRef(), allowParallelGroups, zc::mv(admission));
  {
    auto state = flight->data.lockExclusive();
    state->result = _query_detail::QueryRequestResultAccess::retain(outcome.result);
  }
  {
    auto locked = snapshot->runtime.lockExclusive();
    removeFlight(*locked, *flight.get());
  }
  if (cancellation.isCancelled() &&
      !_query_detail::QueryRequestResultAccess::isRuntimeRejected(outcome.result)) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(
        _query_detail::QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::Cancelled),
        MemoMetadata());
  }
  return outcome;
}

DetailedDemand QueryDatabase::Impl::probeInput(zc::Arc<SnapshotState> snapshot,
                                               CanonicalQueryKey&& key,
                                               const CancellationSource::Token& cancellation) {
  if (cancellation.isCancelled()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(
        _query_detail::QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::Cancelled),
        MemoMetadata());
  }
  RegisteredKind& descriptor = kind(key.kind());
  if (descriptor.descriptor.kind != QueryDescriptorKind::Input) {
    return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                              QueryRuntimeFailure::InvariantViolation),
                          MemoMetadata());
  }
  auto locked = snapshot->runtime.lockShared();
  bool collision = false;
  ZC_IF_SOME(index, exactInputIndex(*locked, key, collision)) {
    const auto& input = locked->inputs[index];
    if (input.value.kind() != QueryValueKind::Value) {
      return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                QueryRuntimeFailure::InvariantViolation),
                            MemoMetadata());
    }
    return DetailedDemand(_query_detail::QueryRequestResultAccess::semantic(input.value.clone()),
                          MemoMetadata(snapshot->revision, input.changedAt, input.durability));
  }
  if (collision) {
    return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                              QueryRuntimeFailure::FingerprintCollision),
                          MemoMetadata());
  }
  return DetailedDemand(
      _query_detail::QueryRequestResultAccess::semantic(QueryValue::absence()),
      MemoMetadata(snapshot->revision, DatabaseRevision(), descriptor.descriptor.durability));
}

DetailedDemand QueryDatabase::Impl::execute(zc::Arc<SnapshotState> snapshot,
                                            RegisteredKind& descriptor, CanonicalQueryKey&& key,
                                            zc::Maybe<Memo>&& prior,
                                            zc::Vector<CanonicalQueryKey>&& activeChain,
                                            zc::Arc<Flight> flight, bool allowParallelGroups,
                                            zc::Arc<const FinalSealAdmission> admission) {
  ZC_IF_SOME(oldMemo, prior) {
    if (descriptor.descriptor.reuse != ReuseClass::RevisionLocal) {
      const size_t durabilityIndex = static_cast<size_t>(oldMemo.metadata.minimumDurability());
      bool green = oldMemo.value != zc::none &&
                   snapshot->lastChanged[durabilityIndex] <= oldMemo.metadata.verifiedAt();
      if (!green) {
        green = true;
        for (const auto& group : oldMemo.dependencies) {
          if (group.kind() == DependencyGroup::Kind::Parallel) {
            for (const auto& dependency : group.dependencies()) {
              if (dependency.inputProbeObservation() != zc::none) {
                appendEvent(*snapshot.get(), key, QueryEventKind::RuntimeFailed);
                return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                          QueryRuntimeFailure::InvariantViolation),
                                      MemoMetadata());
              }
            }
          }
          if (group.kind() == DependencyGroup::Kind::Parallel && allowParallelGroups &&
              group.dependencies().size() > 1) {
            zc::Vector<CanonicalQueryKey> dependencyKeys;
            for (const auto& dependency : group.dependencies()) {
              dependencyKeys.add(dependency.key().clone());
            }
            zc::Vector<CanonicalQueryKey> validationChain;
            for (const auto& active : activeChain) { validationChain.add(active.clone()); }
            validationChain.add(key.clone());
            auto validated =
                demandParallel(snapshot.addRef(), dependencyKeys.asPtr(), validationChain.asPtr(),
                               flight.addRef(), retainAdmission(admission));
            for (auto& result : validated) {
              if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(result.result)) {
                appendEvent(*snapshot.get(), key,
                            failureEvent(_query_detail::QueryRequestResultAccess::runtimeFailure(
                                result.result)));
                return DetailedDemand(zc::mv(result.result), MemoMetadata());
              }
              if (result.metadata.changedAt() > oldMemo.metadata.verifiedAt()) { green = false; }
            }
            if (!green) { break; }
            continue;
          }

          for (const auto& dependency : group.dependencies()) {
            zc::Vector<CanonicalQueryKey> validationChain;
            for (const auto& active : activeChain) { validationChain.add(active.clone()); }
            validationChain.add(key.clone());
            auto validationCancellation = flight->cancellationToken();
            ZC_IF_SOME(observation, dependency.inputProbeObservation()) {
              auto validated =
                  probeInput(snapshot.addRef(), dependency.key().clone(), validationCancellation);
              if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(validated.result)) {
                appendEvent(*snapshot.get(), key,
                            failureEvent(_query_detail::QueryRequestResultAccess::runtimeFailure(
                                validated.result)));
                return DetailedDemand(zc::mv(validated.result), MemoMetadata());
              }
              if (!_query_detail::QueryRequestResultAccess::isSemantic(validated.result)) {
                appendEvent(*snapshot.get(), key, QueryEventKind::RuntimeFailed);
                return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                          QueryRuntimeFailure::InvariantViolation),
                                      MemoMetadata());
              }
              const auto currentObservation =
                  _query_detail::QueryRequestResultAccess::semanticValue(validated.result).kind() ==
                          QueryValueKind::Absence
                      ? InputProbeObservation::Absent
                      : InputProbeObservation::Present;
              if (currentObservation != observation ||
                  (observation == InputProbeObservation::Present &&
                   validated.metadata.changedAt() > oldMemo.metadata.verifiedAt())) {
                green = false;
              }
              continue;
            }
            auto validated =
                demand(snapshot.addRef(), dependency.key().clone(), zc::mv(validationChain),
                       validationCancellation, allowParallelGroups, retainAdmission(admission));
            if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(validated.result)) {
              appendEvent(*snapshot.get(), key,
                          failureEvent(_query_detail::QueryRequestResultAccess::runtimeFailure(
                              validated.result)));
              return DetailedDemand(zc::mv(validated.result), MemoMetadata());
            }
            if (validated.metadata.changedAt() > oldMemo.metadata.verifiedAt()) { green = false; }
          }
          if (!green) { break; }
        }
      }
      if (green && oldMemo.value != zc::none) {
        zc::Vector<DependencyGroup> dependencies;
        for (const auto& group : oldMemo.dependencies) { dependencies.add(group.clone()); }
        const MemoMetadata metadata(snapshot->revision, oldMemo.metadata.changedAt(),
                                    oldMemo.metadata.minimumDurability());
        auto value = ZC_REQUIRE_NONNULL(oldMemo.value).clone();
        zc::Maybe<QueryValue> retainedValue(value.clone());
        zc::Arc<RevisionLocalCapabilityMemoBase> noCapability;
        {
          auto locked = snapshot->runtime.lockExclusive();
          replaceMemo(*locked, Memo(key.clone(), zc::mv(retainedValue), zc::mv(noCapability),
                                    metadata, zc::mv(dependencies)));
          locked->events.add(
              QueryEvent(snapshot->revision, key.clone(), QueryEventKind::GreenReused));
        }
        return DetailedDemand(_query_detail::QueryRequestResultAccess::semantic(zc::mv(value)),
                              metadata);
      }
    }
  }

  if (flight->isCancelled()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(
        _query_detail::QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::Cancelled),
        MemoMetadata());
  }
  zc::Vector<CanonicalQueryKey> providerChain;
  for (const auto& active : activeChain) { providerChain.add(active.clone()); }
  providerChain.add(key.clone());
  QueryContext context(zc::heap<QueryContext::Impl>(*this, snapshot.addRef(), zc::mv(providerChain),
                                                    flight.addRef(), allowParallelGroups,
                                                    zc::mv(admission)));
  if (descriptor.capabilityEvaluator != zc::none) {
    auto completion =
        ZC_REQUIRE_NONNULL(descriptor.capabilityEvaluator)(context, key.canonicalBytes());
    if (context.impl->failure != zc::none) {
      const auto failure = ZC_REQUIRE_NONNULL(context.impl->failure);
      appendEvent(*snapshot.get(), key, failureEvent(failure));
      return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(failure),
                            MemoMetadata());
    }
    if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(completion)) {
      appendEvent(
          *snapshot.get(), key,
          failureEvent(_query_detail::QueryRequestResultAccess::runtimeFailure(completion)));
      return DetailedDemand(zc::mv(completion), MemoMetadata());
    }
    if (flight->isCancelled()) {
      appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
      return DetailedDemand(
          _query_detail::QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::Cancelled),
          MemoMetadata());
    }
    if (_query_detail::QueryRequestResultAccess::isCapabilityPublished(completion)) {
      const auto& capabilityMemo =
          _query_detail::QueryRequestResultAccess::capabilityMemo(completion);
      if (_query_detail::QueryRequestResultAccess::memoDatabase(capabilityMemo) !=
              snapshot->database ||
          _query_detail::QueryRequestResultAccess::memoKey(capabilityMemo) != key ||
          _query_detail::QueryRequestResultAccess::memoRevision(capabilityMemo) !=
              snapshot->revision ||
          _query_detail::QueryRequestResultAccess::memoArenaRevision(capabilityMemo) !=
              snapshot->revision) {
        appendEvent(*snapshot.get(), key, QueryEventKind::RuntimeFailed);
        return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                  QueryRuntimeFailure::InvariantViolation),
                              MemoMetadata());
      }
    } else if (!_query_detail::QueryRequestResultAccess::isCapabilityRejected(completion)) {
      appendEvent(*snapshot.get(), key, QueryEventKind::RuntimeFailed);
      return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                                QueryRuntimeFailure::InvariantViolation),
                            MemoMetadata());
    }

    Durability minimumDurability = Durability::Frozen;
    for (const auto& group : context.impl->dependencies) {
      for (const auto& dependency : group.dependencies()) {
        minimumDurability = lowerDurability(minimumDurability, dependency.durability());
      }
    }
    const MemoMetadata metadata(snapshot->revision, snapshot->revision, minimumDurability);
    zc::Vector<DependencyGroup> dependencies;
    for (const auto& group : context.impl->dependencies) { dependencies.add(group.clone()); }
    zc::Maybe<QueryValue> retainedValue;
    zc::Arc<RevisionLocalCapabilityMemoBase> retainedCapability;
    if (_query_detail::QueryRequestResultAccess::isCapabilityPublished(completion)) {
      retainedCapability =
          _query_detail::QueryRequestResultAccess::retainCapabilityMemo(completion);
    }
    {
      auto locked = snapshot->runtime.lockExclusive();
      replaceMemo(*locked, Memo(key.clone(), zc::mv(retainedValue), zc::mv(retainedCapability),
                                metadata, zc::mv(dependencies)));
      locked->events.add(QueryEvent(snapshot->revision, key.clone(), QueryEventKind::Executed));
    }
    return DetailedDemand(zc::mv(completion), metadata);
  }

  auto candidate = ZC_REQUIRE_NONNULL(descriptor.provider)(context, key.canonicalBytes());
  if (context.impl->failure != zc::none) {
    const auto failure = ZC_REQUIRE_NONNULL(context.impl->failure);
    appendEvent(*snapshot.get(), key, failureEvent(failure));
    return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(failure),
                          MemoMetadata());
  }
  if (_query_detail::QueryRequestResultAccess::isRuntimeRejected(candidate)) {
    appendEvent(*snapshot.get(), key,
                failureEvent(_query_detail::QueryRequestResultAccess::runtimeFailure(candidate)));
    return DetailedDemand(zc::mv(candidate), MemoMetadata());
  }
  if (!_query_detail::QueryRequestResultAccess::isSemantic(candidate)) {
    appendEvent(*snapshot.get(), key, QueryEventKind::RuntimeFailed);
    return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                              QueryRuntimeFailure::InvariantViolation),
                          MemoMetadata());
  }
  if (flight->isCancelled()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(
        _query_detail::QueryRequestResultAccess::runtimeRejected(QueryRuntimeFailure::Cancelled),
        MemoMetadata());
  }
  const auto& candidateValue = _query_detail::QueryRequestResultAccess::semanticValue(candidate);
  const bool verified =
      ZC_REQUIRE_NONNULL(descriptor.verifier)(context, key.canonicalBytes(), candidateValue);
  if (context.impl->failure != zc::none || flight->isCancelled()) {
    const auto failure = flight->isCancelled() ? QueryRuntimeFailure::Cancelled
                                               : ZC_REQUIRE_NONNULL(context.impl->failure);
    appendEvent(*snapshot.get(), key, failureEvent(failure));
    return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(failure),
                          MemoMetadata());
  }
  if (!verified) {
    appendEvent(*snapshot.get(), key, QueryEventKind::VerifierRejected);
    return DetailedDemand(_query_detail::QueryRequestResultAccess::runtimeRejected(
                              QueryRuntimeFailure::VerifierRejected),
                          MemoMetadata());
  }

  Durability minimumDurability = Durability::Frozen;
  for (const auto& group : context.impl->dependencies) {
    for (const auto& dependency : group.dependencies()) {
      minimumDurability = lowerDurability(minimumDurability, dependency.durability());
    }
  }
  DatabaseRevision changedAt = snapshot->revision;
  QueryEventKind event = QueryEventKind::Executed;
  ZC_IF_SOME(oldMemo, prior) {
    if (descriptor.descriptor.reuse != ReuseClass::RevisionLocal && oldMemo.value != zc::none &&
        candidateValue == ZC_REQUIRE_NONNULL(oldMemo.value) &&
        static_cast<uint8_t>(minimumDurability) >=
            static_cast<uint8_t>(oldMemo.metadata.minimumDurability())) {
      changedAt = oldMemo.metadata.changedAt();
      event = QueryEventKind::RecomputedEqual;
    } else if (oldMemo.value != zc::none && candidateValue == ZC_REQUIRE_NONNULL(oldMemo.value) &&
               static_cast<uint8_t>(minimumDurability) <
                   static_cast<uint8_t>(oldMemo.metadata.minimumDurability())) {
      event = QueryEventKind::DurabilityDecreased;
    } else {
      event = QueryEventKind::RecomputedChanged;
    }
  }
  if (descriptor.descriptor.reuse == ReuseClass::RevisionLocal) { changedAt = snapshot->revision; }
  const MemoMetadata metadata(snapshot->revision, changedAt, minimumDurability);
  zc::Vector<DependencyGroup> dependencies;
  for (const auto& group : context.impl->dependencies) { dependencies.add(group.clone()); }
  auto resultValue = candidateValue.clone();
  zc::Maybe<QueryValue> retainedValue(resultValue.clone());
  zc::Arc<RevisionLocalCapabilityMemoBase> noCapability;
  {
    auto locked = snapshot->runtime.lockExclusive();
    replaceMemo(*locked, Memo(key.clone(), zc::mv(retainedValue), zc::mv(noCapability), metadata,
                              zc::mv(dependencies)));
    locked->events.add(QueryEvent(snapshot->revision, key.clone(), event));
  }
  return DetailedDemand(_query_detail::QueryRequestResultAccess::semantic(zc::mv(resultValue)),
                        metadata);
}

}  // namespace zomlang::compiler::query
