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
  Memo(CanonicalQueryKey&& key, zc::Maybe<QueryValue>&& value, MemoMetadata metadata,
       zc::Vector<DependencyGroup>&& dependencies) noexcept
      : key(zc::mv(key)),
        value(zc::mv(value)),
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
    return Memo(key.clone(), zc::mv(clonedValue), metadata, zc::mv(clonedDependencies));
  }

  CanonicalQueryKey key;
  zc::Maybe<QueryValue> value;
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
  SnapshotState(DatabaseRevision revision, zc::ArrayPtr<const DatabaseRevision> changes)
      : revision(revision) {
    ZC_IREQUIRE(changes.size() == 4, "snapshot durability revision count is not four");
    for (size_t index = 0; index < 4; ++index) { lastChanged[index] = changes[index]; }
  }

  DatabaseRevision revision;
  DatabaseRevision lastChanged[4];
  zc::MutexGuarded<SnapshotRuntime> runtime;
};

struct RegisteredKind final {
  RegisteredKind(QueryKindId id, QueryKindContract&& contract,
                 QueryDatabase::ErasedKeyValidator&& keyValidator,
                 zc::Maybe<QueryDatabase::ErasedProvider>&& provider,
                 zc::Maybe<QueryDatabase::ErasedVerifier>&& verifier) noexcept
      : id(id),
        contract(zc::mv(contract)),
        keyValidator(zc::mv(keyValidator)),
        provider(zc::mv(provider)),
        verifier(zc::mv(verifier)) {}
  RegisteredKind(RegisteredKind&&) noexcept = default;
  RegisteredKind& operator=(RegisteredKind&&) noexcept = default;
  ZC_DISALLOW_COPY(RegisteredKind);

  QueryKindId id;
  QueryKindContract contract;
  QueryDatabase::ErasedKeyValidator keyValidator;
  zc::Maybe<QueryDatabase::ErasedProvider> provider;
  zc::Maybe<QueryDatabase::ErasedVerifier> verifier;
};

struct DatabaseData final {
  DatabaseData()
      : current(zc::arc<SnapshotState>(DatabaseRevision(), zc::arrayPtr(initialLastChanged))) {}

  DatabaseRevision initialLastChanged[4] = {};

  zc::Vector<RegisteredKind> kinds;
  zc::Arc<SnapshotState> current;
  bool registrySealed = false;
  bool transactionOpen = false;
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
              bool present) noexcept
      : current(zc::mv(current)),
        baseValue(zc::mv(baseValue)),
        changed(changed),
        existedAtBase(existedAtBase),
        present(present) {}
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
      return QueryEventKind::RuntimeFailed;
  }
  return QueryEventKind::RuntimeFailed;
}

}  // namespace

QueryKindContract::QueryKindContract(zc::String&& domain, ReuseClass reuseClass,
                                     RetentionClass retention, bool isInput,
                                     Durability inputDurability) noexcept
    : domainField(zc::mv(domain)),
      reuseClassField(reuseClass),
      retentionField(retention),
      isInputField(isInput),
      inputDurabilityField(inputDurability) {}

zc::Maybe<QueryKindContract> QueryKindContract::input(zc::StringPtr domain, Durability durability) {
  if (!isCanonicalDomain(domain)) { return zc::none; }
  return QueryKindContract(zc::str(domain), ReuseClass::Semantic, RetentionClass::Retained, true,
                           durability);
}

zc::Maybe<QueryKindContract> QueryKindContract::derived(zc::StringPtr domain, ReuseClass reuseClass,
                                                        RetentionClass retention) {
  if (!isCanonicalDomain(domain)) { return zc::none; }
  return QueryKindContract(zc::str(domain), reuseClass, retention, false, Durability::Frozen);
}

QueryKindContract QueryKindContract::clone() const {
  return QueryKindContract(zc::str(domainField), reuseClassField, retentionField, isInputField,
                           inputDurabilityField);
}

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
  explicit Impl(basic::ThreadPool& scheduler) noexcept : scheduler(scheduler) {}

  enum class KeyAdmission : uint8_t { Trusted, Validate };

  zc::MutexGuarded<DatabaseData> data;
  basic::ThreadPool& scheduler;

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
    const RegisteredKind* kind = nullptr;
    for (const auto& candidate : locked->kinds) {
      if (candidate.contract.domain() == domain) {
        kind = &candidate;
        break;
      }
    }
    if (kind == nullptr) { return zc::none; }
    if (admission == KeyAdmission::Validate && !kind->keyValidator(keyBytes.asPtr())) {
      failure = QueryRuntimeFailure::InvalidKeyEncoding;
      return zc::none;
    }

    static constexpr zc::StringPtr fingerprintDomain = "zom.query-key"_zc;
    zc::Vector<uint8_t> preimage;
    for (char value : fingerprintDomain) { preimage.add(static_cast<uint8_t>(value)); }
    preimage.add(0);
    appendUint32(preimage, static_cast<uint32_t>(domain.size()));
    for (char value : domain) { preimage.add(static_cast<uint8_t>(value)); }
    appendUint32(preimage, static_cast<uint32_t>(keyBytes.size()));
    for (uint8_t value : keyBytes) { preimage.add(value); }
    auto fingerprintBytes = vectorToArray(zc::mv(preimage));
    return CanonicalQueryKey(kind->id, sha256(fingerprintBytes.asPtr()), zc::mv(keyBytes));
  }

  zc::Maybe<CanonicalQueryKey> makeKey(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                       QueryRuntimeFailure& failure) const {
    return makeKeyInternal(domain, zc::mv(keyBytes), failure, KeyAdmission::Trusted);
  }

  zc::Maybe<CanonicalQueryKey> makeValidatedKey(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                                QueryRuntimeFailure& failure) const {
    return makeKeyInternal(domain, zc::mv(keyBytes), failure, KeyAdmission::Validate);
  }

  RegisteredKind& kind(QueryKindId id) {
    auto locked = data.lockExclusive();
    ZC_IREQUIRE(id.value() < locked->kinds.size(), "query kind id is out of range");
    return locked->kinds[id.value()];
  }

  void appendEvent(const SnapshotState& snapshot, const CanonicalQueryKey& key,
                   QueryEventKind kind) {
    auto locked = snapshot.runtime.lockExclusive();
    locked->events.add(QueryEvent(snapshot.revision, key.clone(), kind));
  }

  DetailedDemand demand(zc::Arc<SnapshotState> snapshot, CanonicalQueryKey&& key,
                        zc::Vector<CanonicalQueryKey>&& activeChain,
                        const CancellationSource::Token& cancellation, bool allowParallelGroups);

  DetailedDemand probeInput(zc::Arc<SnapshotState> snapshot, CanonicalQueryKey&& key,
                            const CancellationSource::Token& cancellation);

  zc::Vector<DetailedDemand> demandParallel(zc::Arc<SnapshotState> snapshot,
                                            zc::ArrayPtr<const CanonicalQueryKey> keys,
                                            zc::ArrayPtr<const CanonicalQueryKey> activeChain,
                                            zc::Arc<Flight> flight);

  DetailedDemand execute(zc::Arc<SnapshotState> snapshot, RegisteredKind& kind,
                         CanonicalQueryKey&& key, zc::Maybe<Memo>&& prior,
                         zc::Vector<CanonicalQueryKey>&& activeChain, zc::Arc<Flight> flight,
                         bool allowParallelGroups);
};

struct QueryContext::Impl final {
  Impl(QueryDatabase::Impl& database, zc::Arc<SnapshotState> snapshot,
       zc::Vector<CanonicalQueryKey>&& activeChain, zc::Arc<Flight> flight,
       bool allowParallelGroups) noexcept
      : database(database),
        snapshot(zc::mv(snapshot)),
        activeChain(zc::mv(activeChain)),
        flight(zc::mv(flight)),
        allowParallelGroups(allowParallelGroups) {}

  QueryDatabase::Impl& database;
  zc::Arc<SnapshotState> snapshot;
  zc::Vector<CanonicalQueryKey> activeChain;
  zc::Arc<Flight> flight;
  bool allowParallelGroups;
  zc::Vector<DependencyGroup> dependencies;
  zc::Maybe<QueryRuntimeFailure> failure;
};

struct QuerySnapshot::Impl final {
  Impl(QueryDatabase::Impl& database, zc::Arc<SnapshotState>&& snapshot) noexcept
      : database(database), snapshot(zc::mv(snapshot)) {}
  QueryDatabase::Impl& database;
  zc::Arc<SnapshotState> snapshot;
};

struct InputTransaction::Impl final {
  Impl(QueryDatabase::Impl& database, DatabaseRevision baseRevision,
       zc::Vector<StagedInput>&& inputs) noexcept
      : database(database), baseRevision(baseRevision), inputs(zc::mv(inputs)) {}
  QueryDatabase::Impl& database;
  DatabaseRevision baseRevision;
  zc::Vector<StagedInput> inputs;
  bool closed = false;
};

QueryContext::QueryContext(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
QueryContext::QueryContext(QueryContext&&) noexcept = default;
QueryContext& QueryContext::operator=(QueryContext&&) noexcept = default;
QueryContext::~QueryContext() noexcept(false) = default;

bool QueryContext::isCancelled() const { return impl->flight->isCancelled(); }

zc::Vector<DetailedDemand> QueryDatabase::Impl::demandParallel(
    zc::Arc<SnapshotState> snapshot, zc::ArrayPtr<const CanonicalQueryKey> keys,
    zc::ArrayPtr<const CanonicalQueryKey> activeChain, zc::Arc<Flight> flight) {
  auto state = zc::arc<ParallelDemandState>(keys.size());
  for (size_t index = 0; index < keys.size(); ++index) {
    zc::Vector<CanonicalQueryKey> chain;
    for (const auto& active : activeChain) { chain.add(active.clone()); }
    auto cancellation = flight->cancellationToken();
    scheduler.enqueue([&database = *this, snapshot = snapshot.addRef(), key = keys[index].clone(),
                       chain = zc::mv(chain), cancellation = zc::mv(cancellation),
                       state = state.addRef(), index]() mutable {
      zc::Maybe<DetailedDemand> outcome;
      auto exception = zc::runCatchingExceptions([&]() {
        outcome =
            database.demand(snapshot.addRef(), zc::mv(key), zc::mv(chain), cancellation, false);
      });
      if (exception != zc::none) {
        outcome = DetailedDemand(
            QueryRequestResult::failed(QueryRuntimeFailure::InvariantViolation), MemoMetadata());
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
    return QueryRequestResult::failed(keyFailure);
  }
  auto demandedKey = ZC_REQUIRE_NONNULL(key).clone();
  zc::Vector<CanonicalQueryKey> chain;
  for (const auto& active : impl->activeChain) { chain.add(active.clone()); }
  auto cancellation = impl->flight->cancellationToken();
  auto demand = impl->database.demand(impl->snapshot.addRef(), zc::mv(ZC_REQUIRE_NONNULL(key)),
                                      zc::mv(chain), cancellation, impl->allowParallelGroups);
  if (!demand.result.isCompleted()) {
    impl->failure = demand.result.failure();
    return zc::mv(demand.result);
  }
  impl->dependencies.add(DependencyGroup::sequential(DependencyRecord(
      zc::mv(demandedKey), demand.metadata.changedAt(), demand.metadata.minimumDurability())));
  return zc::mv(demand.result);
}

QueryRequestResult QueryContext::probeInputEncoded(zc::StringPtr domain,
                                                   zc::Array<uint8_t>&& keyBytes) {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeValidatedKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) {
    impl->failure = keyFailure;
    return QueryRequestResult::failed(keyFailure);
  }
  auto demandedKey = ZC_REQUIRE_NONNULL(key).clone();
  auto cancellation = impl->flight->cancellationToken();
  auto demand = impl->database.probeInput(impl->snapshot.addRef(), zc::mv(ZC_REQUIRE_NONNULL(key)),
                                          cancellation);
  if (!demand.result.isCompleted()) {
    impl->failure = demand.result.failure();
    return zc::mv(demand.result);
  }
  const auto observation = demand.result.value().kind() == QueryValueKind::Absence
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
    failed.add(QueryRequestResult::failed(QueryRuntimeFailure::InvariantViolation));
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
      failed.add(QueryRequestResult::failed(keyFailure));
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
                                               impl->activeChain.asPtr(), impl->flight.addRef());
  if (demands.size() != keys.size()) {
    impl->failure = QueryRuntimeFailure::InvariantViolation;
    zc::Vector<QueryRequestResult> failed;
    failed.add(QueryRequestResult::failed(QueryRuntimeFailure::InvariantViolation));
    return failed;
  }
  zc::Vector<zc::Maybe<QueryRequestResult>> orderedResults;
  for (size_t index = 0; index < keys.size(); ++index) { orderedResults.add(zc::none); }
  zc::Vector<DependencyRecord> dependencies;
  bool failed = false;
  for (size_t index = 0; index < keys.size(); ++index) {
    auto& demand = demands[index];
    if (!demand.result.isCompleted()) {
      if (!failed) { impl->failure = demand.result.failure(); }
      failed = true;
      orderedResults[resultPositions[index]] = zc::mv(demand.result);
      continue;
    }
    dependencies.add(DependencyRecord(keys[index].clone(), demand.metadata.changedAt(),
                                      demand.metadata.minimumDurability()));
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

QueryRequestResult QuerySnapshot::getEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                             const CancellationSource::Token& cancellation) const {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return QueryRequestResult::failed(keyFailure); }
  zc::Vector<CanonicalQueryKey> chain;
  auto demand = impl->database.demand(impl->snapshot.addRef(), zc::mv(ZC_REQUIRE_NONNULL(key)),
                                      zc::mv(chain), cancellation, true);
  return zc::mv(demand.result);
}

QueryRequestResult QuerySnapshot::probeInputEncoded(
    zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
    const CancellationSource::Token& cancellation) const {
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeValidatedKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return QueryRequestResult::failed(keyFailure); }
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
  if (descriptor.contract.isInput() ||
      descriptor.contract.retention() != RetentionClass::Evictable) {
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
    return locked->memos[index].value != zc::none;
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
InputTransaction& InputTransaction::operator=(InputTransaction&&) noexcept = default;
InputTransaction::~InputTransaction() noexcept(false) {
  if (impl.get() != nullptr && !impl->closed) { abandon(); }
}

bool InputTransaction::stageEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes,
                                    QueryValue&& value) {
  if (impl->closed) { return false; }
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeValidatedKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return false; }
  RegisteredKind& kind = impl->database.kind(ZC_REQUIRE_NONNULL(key).kind());
  if (!kind.contract.isInput()) { return false; }

  for (auto& input : impl->inputs) {
    if (!keyFingerprintMatches(input.current.key, ZC_REQUIRE_NONNULL(key))) { continue; }
    if (input.current.key != ZC_REQUIRE_NONNULL(key)) { return false; }
    if (input.current.durability == Durability::Frozen && input.existedAtBase &&
        input.baseValue != value) {
      return false;
    }
    input.current.value = zc::mv(value);
    input.present = true;
    input.refreshChanged();
    return true;
  }

  auto base = QueryValue::absence();
  impl->inputs.add(StagedInput(InputEntry(zc::mv(ZC_REQUIRE_NONNULL(key)), zc::mv(value),
                                          DatabaseRevision(), kind.contract.inputDurability()),
                               zc::mv(base), true, false, true));
  return true;
}

bool InputTransaction::eraseEncoded(zc::StringPtr domain, zc::Array<uint8_t>&& keyBytes) {
  if (impl->closed) { return false; }
  QueryRuntimeFailure keyFailure;
  auto key = impl->database.makeValidatedKey(domain, zc::mv(keyBytes), keyFailure);
  if (key == zc::none) { return false; }
  RegisteredKind& kind = impl->database.kind(ZC_REQUIRE_NONNULL(key).kind());
  if (!kind.contract.isInput() || kind.contract.inputDurability() == Durability::Frozen) {
    return false;
  }

  for (auto& input : impl->inputs) {
    if (!keyFingerprintMatches(input.current.key, ZC_REQUIRE_NONNULL(key))) { continue; }
    if (input.current.key != ZC_REQUIRE_NONNULL(key) || !input.present) { return false; }
    input.present = false;
    input.refreshChanged();
    return true;
  }
  return false;
}

zc::Maybe<DatabaseRevision> InputTransaction::commit() {
  if (impl->closed) { return zc::none; }
  auto database = impl->database.data.lockExclusive();
  if (database->current->revision != impl->baseRevision || !database->transactionOpen) {
    impl->closed = true;
    return zc::none;
  }
  if (impl->baseRevision.value() == UINT64_MAX) {
    database->transactionOpen = false;
    impl->closed = true;
    return zc::none;
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
  auto next = zc::arc<SnapshotState>(nextRevision, zc::arrayPtr(nextLastChanged));

  {
    auto nextRuntime = next->runtime.lockExclusive();
    for (auto& input : impl->inputs) {
      if (input.changed) { input.current.changedAt = nextRevision; }
      if (!input.present) { continue; }
      nextRuntime->inputs.add(input.current.clone());
    }
    auto priorRuntime = database->current->runtime.lockShared();
    for (const auto& memo : priorRuntime->memos) { nextRuntime->memos.add(memo.clone()); }
  }

  database->current = zc::mv(next);
  database->transactionOpen = false;
  impl->closed = true;
  return nextRevision;
}

void InputTransaction::abandon() {
  if (impl->closed) { return; }
  auto database = impl->database.data.lockExclusive();
  database->transactionOpen = false;
  impl->closed = true;
}

QueryDatabase::QueryDatabase(basic::ThreadPool& scheduler) : impl(zc::heap<Impl>(scheduler)) {}
QueryDatabase::~QueryDatabase() noexcept(false) = default;
QueryDatabase::QueryDatabase(QueryDatabase&&) noexcept = default;
QueryDatabase& QueryDatabase::operator=(QueryDatabase&&) noexcept = default;

zc::Maybe<QueryKindId> QueryDatabase::installInput(QueryKindContract&& contract,
                                                   ErasedKeyValidator&& keyValidator) {
  if (!contract.isInput()) { return zc::none; }
  auto locked = impl->data.lockExclusive();
  if (locked->registrySealed || locked->kinds.size() >= UINT32_MAX) { return zc::none; }
  for (const auto& kind : locked->kinds) {
    if (kind.contract.domain() == contract.domain()) { return zc::none; }
  }
  const QueryKindId id(static_cast<uint32_t>(locked->kinds.size()));
  zc::Maybe<ErasedProvider> noProvider;
  zc::Maybe<ErasedVerifier> noVerifier;
  locked->kinds.add(RegisteredKind(id, zc::mv(contract), zc::mv(keyValidator), zc::mv(noProvider),
                                   zc::mv(noVerifier)));
  return id;
}

zc::Maybe<QueryKindId> QueryDatabase::installDerived(QueryKindContract&& contract,
                                                     ErasedKeyValidator&& keyValidator,
                                                     ErasedProvider&& provider,
                                                     ErasedVerifier&& verifier) {
  if (contract.isInput()) { return zc::none; }
  auto locked = impl->data.lockExclusive();
  if (locked->registrySealed || locked->kinds.size() >= UINT32_MAX) { return zc::none; }
  for (const auto& kind : locked->kinds) {
    if (kind.contract.domain() == contract.domain()) { return zc::none; }
  }
  const QueryKindId id(static_cast<uint32_t>(locked->kinds.size()));
  zc::Maybe<ErasedProvider> retainedProvider(zc::mv(provider));
  zc::Maybe<ErasedVerifier> retainedVerifier(zc::mv(verifier));
  locked->kinds.add(RegisteredKind(id, zc::mv(contract), zc::mv(keyValidator),
                                   zc::mv(retainedProvider), zc::mv(retainedVerifier)));
  return id;
}

QuerySnapshot QueryDatabase::snapshot() {
  auto locked = impl->data.lockExclusive();
  locked->registrySealed = true;
  return QuerySnapshot(zc::heap<QuerySnapshot::Impl>(*impl, locked->current.addRef()));
}

zc::Maybe<InputTransaction> QueryDatabase::beginInputTransaction() {
  auto locked = impl->data.lockExclusive();
  if (locked->transactionOpen) { return zc::none; }
  locked->registrySealed = true;
  locked->transactionOpen = true;
  zc::Vector<StagedInput> inputs;
  auto runtime = locked->current->runtime.lockShared();
  for (const auto& input : runtime->inputs) {
    inputs.add(StagedInput(input.clone(), input.value.clone(), false, true, true));
  }
  return InputTransaction(
      zc::heap<InputTransaction::Impl>(*impl, locked->current->revision, zc::mv(inputs)));
}

DetailedDemand QueryDatabase::Impl::demand(zc::Arc<SnapshotState> snapshot, CanonicalQueryKey&& key,
                                           zc::Vector<CanonicalQueryKey>&& activeChain,
                                           const CancellationSource::Token& cancellation,
                                           bool allowParallelGroups) {
  if (cancellation.isCancelled()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::Cancelled),
                          MemoMetadata());
  }
  if (containsKey(activeChain.asPtr(), key)) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cycle);
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::Cycle), MemoMetadata());
  }

  RegisteredKind& descriptor = kind(key.kind());
  if (descriptor.contract.isInput()) {
    auto locked = snapshot->runtime.lockShared();
    bool collision = false;
    ZC_IF_SOME(index, exactInputIndex(*locked, key, collision)) {
      const auto& input = locked->inputs[index];
      return DetailedDemand(QueryRequestResult::completed(input.value.clone()),
                            MemoMetadata(snapshot->revision, input.changedAt, input.durability));
    }
    return DetailedDemand(
        QueryRequestResult::failed(collision ? QueryRuntimeFailure::FingerprintCollision
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
      if (memo.metadata.verifiedAt() == snapshot->revision && memo.value != zc::none) {
        auto value = ZC_REQUIRE_NONNULL(memo.value).clone();
        const auto metadata = memo.metadata;
        locked->events.add(
            QueryEvent(snapshot->revision, key.clone(), QueryEventKind::GreenReused));
        return DetailedDemand(QueryRequestResult::completed(zc::mv(value)), metadata);
      }
      prior = memo.clone();
    }
    if (collision) {
      return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::FingerprintCollision),
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
          return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::Cycle),
                                MemoMetadata());
        }
      }
      locked->events.add(
          QueryEvent(snapshot->revision, key.clone(), QueryEventKind::SingleFlightJoined));
    } else {
      if (collision) {
        return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::FingerprintCollision),
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
        return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::Cancelled),
                              MemoMetadata());
      }
      auto state = flight->data.lockExclusive();
      state.wait([](const FlightData& data) { return data.result != zc::none; },
                 10 * zc::MILLISECONDS);
      ZC_IF_SOME(result, state->result) {
        auto clonedResult = result.clone();
        if (waitEdgeAdded) {
          state.release();
          auto locked = snapshot->runtime.lockExclusive();
          removeWaitEdge(*locked, parent, key);
        }
        if (!clonedResult.isCompleted()) {
          return DetailedDemand(zc::mv(clonedResult), MemoMetadata());
        }
        auto locked = snapshot->runtime.lockShared();
        bool collision = false;
        ZC_IF_SOME(index, exactMemoIndex(*locked, key, collision)) {
          return DetailedDemand(zc::mv(clonedResult), locked->memos[index].metadata);
        }
        return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::InvariantViolation),
                              MemoMetadata());
      }
    }
  }

  auto outcome = execute(snapshot.addRef(), descriptor, key.clone(), zc::mv(prior),
                         zc::mv(activeChain), flight.addRef(), allowParallelGroups);
  {
    auto state = flight->data.lockExclusive();
    state->result = outcome.result.clone();
  }
  {
    auto locked = snapshot->runtime.lockExclusive();
    removeFlight(*locked, *flight.get());
  }
  if (cancellation.isCancelled() && outcome.result.isCompleted()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::Cancelled),
                          MemoMetadata());
  }
  return outcome;
}

DetailedDemand QueryDatabase::Impl::probeInput(zc::Arc<SnapshotState> snapshot,
                                               CanonicalQueryKey&& key,
                                               const CancellationSource::Token& cancellation) {
  if (cancellation.isCancelled()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::Cancelled),
                          MemoMetadata());
  }
  RegisteredKind& descriptor = kind(key.kind());
  if (!descriptor.contract.isInput()) {
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::InvariantViolation),
                          MemoMetadata());
  }
  auto locked = snapshot->runtime.lockShared();
  bool collision = false;
  ZC_IF_SOME(index, exactInputIndex(*locked, key, collision)) {
    const auto& input = locked->inputs[index];
    if (input.value.kind() != QueryValueKind::Value) {
      return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::InvariantViolation),
                            MemoMetadata());
    }
    return DetailedDemand(QueryRequestResult::completed(input.value.clone()),
                          MemoMetadata(snapshot->revision, input.changedAt, input.durability));
  }
  if (collision) {
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::FingerprintCollision),
                          MemoMetadata());
  }
  return DetailedDemand(
      QueryRequestResult::completed(QueryValue::absence()),
      MemoMetadata(snapshot->revision, DatabaseRevision(), descriptor.contract.inputDurability()));
}

DetailedDemand QueryDatabase::Impl::execute(zc::Arc<SnapshotState> snapshot,
                                            RegisteredKind& descriptor, CanonicalQueryKey&& key,
                                            zc::Maybe<Memo>&& prior,
                                            zc::Vector<CanonicalQueryKey>&& activeChain,
                                            zc::Arc<Flight> flight, bool allowParallelGroups) {
  ZC_IF_SOME(oldMemo, prior) {
    if (descriptor.contract.reuseClass() != ReuseClass::RevisionLocal) {
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
                return DetailedDemand(
                    QueryRequestResult::failed(QueryRuntimeFailure::InvariantViolation),
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
            auto validated = demandParallel(snapshot.addRef(), dependencyKeys.asPtr(),
                                            validationChain.asPtr(), flight.addRef());
            for (auto& result : validated) {
              if (!result.result.isCompleted()) {
                appendEvent(*snapshot.get(), key, failureEvent(result.result.failure()));
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
              if (!validated.result.isCompleted()) {
                appendEvent(*snapshot.get(), key, failureEvent(validated.result.failure()));
                return DetailedDemand(zc::mv(validated.result), MemoMetadata());
              }
              const auto currentObservation =
                  validated.result.value().kind() == QueryValueKind::Absence
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
                       validationCancellation, allowParallelGroups);
            if (!validated.result.isCompleted()) {
              appendEvent(*snapshot.get(), key, failureEvent(validated.result.failure()));
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
        {
          auto locked = snapshot->runtime.lockExclusive();
          replaceMemo(*locked,
                      Memo(key.clone(), zc::mv(retainedValue), metadata, zc::mv(dependencies)));
          locked->events.add(
              QueryEvent(snapshot->revision, key.clone(), QueryEventKind::GreenReused));
        }
        return DetailedDemand(QueryRequestResult::completed(zc::mv(value)), metadata);
      }
    }
  }

  if (flight->isCancelled()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::Cancelled),
                          MemoMetadata());
  }
  zc::Vector<CanonicalQueryKey> providerChain;
  for (const auto& active : activeChain) { providerChain.add(active.clone()); }
  providerChain.add(key.clone());
  QueryContext context(zc::heap<QueryContext::Impl>(*this, snapshot.addRef(), zc::mv(providerChain),
                                                    flight.addRef(), allowParallelGroups));
  auto candidate = ZC_REQUIRE_NONNULL(descriptor.provider)(context, key.canonicalBytes());
  if (context.impl->failure != zc::none) {
    const auto failure = ZC_REQUIRE_NONNULL(context.impl->failure);
    appendEvent(*snapshot.get(), key, failureEvent(failure));
    return DetailedDemand(QueryRequestResult::failed(failure), MemoMetadata());
  }
  if (!candidate.isCompleted()) {
    appendEvent(*snapshot.get(), key, failureEvent(candidate.failure()));
    return DetailedDemand(zc::mv(candidate), MemoMetadata());
  }
  if (flight->isCancelled()) {
    appendEvent(*snapshot.get(), key, QueryEventKind::Cancelled);
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::Cancelled),
                          MemoMetadata());
  }
  const bool verified =
      ZC_REQUIRE_NONNULL(descriptor.verifier)(context, key.canonicalBytes(), candidate.value());
  if (context.impl->failure != zc::none || flight->isCancelled()) {
    const auto failure = flight->isCancelled() ? QueryRuntimeFailure::Cancelled
                                               : ZC_REQUIRE_NONNULL(context.impl->failure);
    appendEvent(*snapshot.get(), key, failureEvent(failure));
    return DetailedDemand(QueryRequestResult::failed(failure), MemoMetadata());
  }
  if (!verified) {
    appendEvent(*snapshot.get(), key, QueryEventKind::VerifierRejected);
    return DetailedDemand(QueryRequestResult::failed(QueryRuntimeFailure::VerifierRejected),
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
    if (descriptor.contract.reuseClass() != ReuseClass::RevisionLocal &&
        oldMemo.value != zc::none && candidate.value() == ZC_REQUIRE_NONNULL(oldMemo.value) &&
        static_cast<uint8_t>(minimumDurability) >=
            static_cast<uint8_t>(oldMemo.metadata.minimumDurability())) {
      changedAt = oldMemo.metadata.changedAt();
      event = QueryEventKind::RecomputedEqual;
    } else if (oldMemo.value != zc::none &&
               candidate.value() == ZC_REQUIRE_NONNULL(oldMemo.value) &&
               static_cast<uint8_t>(minimumDurability) <
                   static_cast<uint8_t>(oldMemo.metadata.minimumDurability())) {
      event = QueryEventKind::DurabilityDecreased;
    } else {
      event = QueryEventKind::RecomputedChanged;
    }
  }
  if (descriptor.contract.reuseClass() == ReuseClass::RevisionLocal) {
    changedAt = snapshot->revision;
  }
  const MemoMetadata metadata(snapshot->revision, changedAt, minimumDurability);
  zc::Vector<DependencyGroup> dependencies;
  for (const auto& group : context.impl->dependencies) { dependencies.add(group.clone()); }
  auto resultValue = candidate.value().clone();
  zc::Maybe<QueryValue> retainedValue(resultValue.clone());
  {
    auto locked = snapshot->runtime.lockExclusive();
    replaceMemo(*locked, Memo(key.clone(), zc::mv(retainedValue), metadata, zc::mv(dependencies)));
    locked->events.add(QueryEvent(snapshot->revision, key.clone(), event));
  }
  return DetailedDemand(QueryRequestResult::completed(zc::mv(resultValue)), metadata);
}

}  // namespace zomlang::compiler::query
