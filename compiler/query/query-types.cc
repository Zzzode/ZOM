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

#include "compiler/query/query-types.h"

#include "zc/core/debug.h"

namespace zomlang::compiler::query {
namespace {

bool bytesLess(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
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

bool consumeUint32(zc::ArrayPtr<const uint8_t> bytes, size_t& cursor, uint32_t& value) {
  if (cursor > bytes.size() || bytes.size() - cursor < 4) { return false; }
  value = (static_cast<uint32_t>(bytes[cursor]) << 24) |
          (static_cast<uint32_t>(bytes[cursor + 1]) << 16) |
          (static_cast<uint32_t>(bytes[cursor + 2]) << 8) |
          static_cast<uint32_t>(bytes[cursor + 3]);
  cursor += 4;
  return true;
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

}  // namespace

namespace _query_detail {

class QueryDatabaseIdentityToken final : public zc::AtomicRefcounted {
public:
  QueryDatabaseIdentityToken() = default;
  ~QueryDatabaseIdentityToken() noexcept(false) override = default;
  ZC_DISALLOW_COPY_AND_MOVE(QueryDatabaseIdentityToken);
};

}  // namespace _query_detail

QueryDatabaseIdentity::QueryDatabaseIdentity(
    zc::Arc<const _query_detail::QueryDatabaseIdentityToken>&& token) noexcept
    : tokenField(zc::mv(token)) {
  ZC_IREQUIRE(tokenField != nullptr, "query database identity has no token");
}

QueryDatabaseIdentity::QueryDatabaseIdentity(QueryDatabaseIdentity&&) noexcept = default;
QueryDatabaseIdentity& QueryDatabaseIdentity::operator=(QueryDatabaseIdentity&&) noexcept = default;
QueryDatabaseIdentity::~QueryDatabaseIdentity() noexcept(false) = default;

QueryDatabaseIdentity QueryDatabaseIdentity::create() {
  zc::Arc<const _query_detail::QueryDatabaseIdentityToken> token =
      zc::arc<_query_detail::QueryDatabaseIdentityToken>();
  return QueryDatabaseIdentity(zc::mv(token));
}

QueryDatabaseIdentity QueryDatabaseIdentity::retain() const {
  return QueryDatabaseIdentity(tokenField.addRef());
}

bool QueryDatabaseIdentity::operator==(const QueryDatabaseIdentity& other) const noexcept {
  return tokenField == other.tokenField;
}

CapabilityFailureEnvelope::CapabilityFailureEnvelope(zc::String&& descriptorDomain,
                                                     CapabilityFailureKind kind,
                                                     zc::Array<uint8_t>&& canonicalPayload)
    : descriptorDomainField(zc::mv(descriptorDomain)),
      kindField(kind),
      canonicalPayloadField(zc::mv(canonicalPayload)) {
  ZC_IREQUIRE(isCanonicalDomain(descriptorDomainField),
              "capability rejection requires a canonical descriptor domain");
  ZC_IREQUIRE(canonicalPayloadField.size() != 0 && canonicalPayloadField.size() <= UINT32_MAX,
              "capability rejection requires a bounded canonical payload");

  static constexpr zc::StringPtr envelopeDomain = "zom.query.capability-failure"_zc;
  zc::Vector<uint8_t> encoded;
  for (char value : envelopeDomain) { encoded.add(static_cast<uint8_t>(value)); }
  encoded.add(0);
  appendUint32(encoded, static_cast<uint32_t>(descriptorDomainField.size()));
  for (char value : descriptorDomainField) { encoded.add(static_cast<uint8_t>(value)); }
  encoded.add(static_cast<uint8_t>(kindField));
  appendUint32(encoded, static_cast<uint32_t>(canonicalPayloadField.size()));
  for (uint8_t value : canonicalPayloadField) { encoded.add(value); }
  canonicalBytesField = vectorToArray(zc::mv(encoded));
}

zc::Maybe<CapabilityFailureEnvelope> CapabilityFailureEnvelope::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  static constexpr zc::StringPtr envelopeDomain = "zom.query.capability-failure"_zc;
  const size_t prefixSize = envelopeDomain.size() + 1;
  if (bytes.size() < prefixSize + 4 + 1 + 4) { return zc::none; }
  for (size_t index = 0; index < envelopeDomain.size(); ++index) {
    if (bytes[index] != static_cast<uint8_t>(envelopeDomain[index])) { return zc::none; }
  }
  if (bytes[envelopeDomain.size()] != 0) { return zc::none; }

  size_t cursor = prefixSize;
  uint32_t descriptorSize = 0;
  if (!consumeUint32(bytes, cursor, descriptorSize) || descriptorSize == 0 ||
      cursor > bytes.size() || descriptorSize > bytes.size() - cursor) {
    return zc::none;
  }
  auto descriptorBytes = bytes.slice(cursor, cursor + descriptorSize);
  cursor += descriptorSize;
  auto descriptorCharacters = zc::heapArray<char>(static_cast<size_t>(descriptorSize) + 1);
  for (size_t index = 0; index < descriptorSize; ++index) {
    descriptorCharacters[index] = static_cast<char>(descriptorBytes[index]);
  }
  descriptorCharacters[descriptorSize] = '\0';
  zc::String descriptorDomain(zc::mv(descriptorCharacters));
  if (!isCanonicalDomain(descriptorDomain)) { return zc::none; }
  if (cursor == bytes.size()) { return zc::none; }
  const uint8_t rawKind = bytes[cursor++];
  if (rawKind != static_cast<uint8_t>(CapabilityFailureKind::SourceRejected) &&
      rawKind != static_cast<uint8_t>(CapabilityFailureKind::KeyRejected)) {
    return zc::none;
  }

  uint32_t payloadSize = 0;
  if (!consumeUint32(bytes, cursor, payloadSize) || payloadSize == 0 || cursor > bytes.size() ||
      payloadSize != bytes.size() - cursor) {
    return zc::none;
  }
  auto payload = zc::heapArray<uint8_t>(bytes.slice(cursor, bytes.size()));
  CapabilityFailureEnvelope decoded(zc::mv(descriptorDomain),
                                    static_cast<CapabilityFailureKind>(rawKind), zc::mv(payload));
  if (decoded.canonicalBytes() != bytes) { return zc::none; }
  return decoded;
}

bool QueryKeyFingerprint::operator<(const QueryKeyFingerprint& other) const noexcept {
  return bytesLess(bytes(), other.bytes());
}

zc::Maybe<QueryKeyFingerprint> QueryKeyFingerprint::fromBytes(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() != 32) { return zc::none; }
  QueryKeyFingerprint result;
  for (size_t index = 0; index < bytes.size(); ++index) { result.valueField[index] = bytes[index]; }
  return result;
}

CanonicalQueryKey::CanonicalQueryKey(QueryKindId kind, const QueryKeyFingerprint& fingerprint,
                                     zc::Array<uint8_t>&& canonicalBytes) noexcept
    : kindField(kind), fingerprintField(fingerprint), canonicalBytesField(zc::mv(canonicalBytes)) {}

CanonicalQueryKey CanonicalQueryKey::clone() const {
  return CanonicalQueryKey(kindField, fingerprintField,
                           zc::heapArray<uint8_t>(canonicalBytesField.asPtr()));
}

bool CanonicalQueryKey::operator==(const CanonicalQueryKey& other) const noexcept {
  return kindField == other.kindField && fingerprintField == other.fingerprintField &&
         canonicalBytesField.asPtr() == other.canonicalBytesField.asPtr();
}

bool CanonicalQueryKey::operator<(const CanonicalQueryKey& other) const noexcept {
  if (kindField != other.kindField) { return kindField < other.kindField; }
  if (fingerprintField != other.fingerprintField) {
    return fingerprintField < other.fingerprintField;
  }
  return bytesLess(canonicalBytesField.asPtr(), other.canonicalBytesField.asPtr());
}

QueryValue::QueryValue(QueryValueKind kind, zc::Array<uint8_t>&& canonicalBytes) noexcept
    : kindField(kind), canonicalBytesField(zc::mv(canonicalBytes)) {}

QueryValue QueryValue::value(zc::Array<uint8_t>&& canonicalBytes) {
  return QueryValue(QueryValueKind::Value, zc::mv(canonicalBytes));
}

QueryValue QueryValue::absence() {
  return QueryValue(QueryValueKind::Absence, zc::heapArray<uint8_t>(0));
}

QueryValue QueryValue::semanticFailure(zc::Array<uint8_t>&& canonicalBytes) {
  return QueryValue(QueryValueKind::SemanticFailure, zc::mv(canonicalBytes));
}

QueryValue QueryValue::clone() const {
  return QueryValue(kindField, zc::heapArray<uint8_t>(canonicalBytesField.asPtr()));
}

bool QueryValue::operator==(const QueryValue& other) const noexcept {
  return kindField == other.kindField &&
         canonicalBytesField.asPtr() == other.canonicalBytesField.asPtr();
}

struct SnapshotCapabilityArena::Impl final {
  Impl(DatabaseRevision revision, zc::Arc<SemanticContextCapabilityArena>&& context) noexcept
      : revision(revision), context(zc::mv(context)) {}

  DatabaseRevision revision;
  zc::Arc<SemanticContextCapabilityArena> context;
};

SnapshotCapabilityArena::SnapshotCapabilityArena(DatabaseRevision revision,
                                                 zc::Arc<SemanticContextCapabilityArena>&& context)
    : impl(zc::heap<Impl>(revision, zc::mv(context))) {}
SnapshotCapabilityArena::~SnapshotCapabilityArena() noexcept(false) = default;

DatabaseRevision SnapshotCapabilityArena::revision() const noexcept { return impl->revision; }

const SemanticContextCapabilityResources& SnapshotCapabilityArena::resources() const {
  return impl->context->resources();
}

struct RevisionLocalCapabilityMemoBase::Impl final {
  Impl(QueryDatabaseIdentity&& database, CanonicalQueryKey&& key, DatabaseRevision revision,
       zc::Arc<SnapshotCapabilityArena>&& arena,
       zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies,
       zc::Array<uint8_t>&& stableWitness) noexcept
      : database(zc::mv(database)),
        key(zc::mv(key)),
        revision(revision),
        arena(zc::mv(arena)),
        retainedDependencies(zc::mv(retainedDependencies)),
        stableWitness(zc::mv(stableWitness)) {}

  QueryDatabaseIdentity database;
  CanonicalQueryKey key;
  DatabaseRevision revision;
  zc::Arc<SnapshotCapabilityArena> arena;
  zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>> retainedDependencies;
  zc::Array<uint8_t> stableWitness;
};

RevisionLocalCapabilityMemoBase::RevisionLocalCapabilityMemoBase(
    QueryDatabaseIdentity&& database, CanonicalQueryKey&& key, DatabaseRevision revision,
    zc::Arc<SnapshotCapabilityArena>&& arena,
    zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies,
    zc::Array<uint8_t>&& stableWitness)
    : impl(zc::heap<Impl>(zc::mv(database), zc::mv(key), revision, zc::mv(arena),
                          zc::mv(retainedDependencies), zc::mv(stableWitness))) {}

RevisionLocalCapabilityMemoBase::~RevisionLocalCapabilityMemoBase() noexcept(false) = default;

const QueryDatabaseIdentity& RevisionLocalCapabilityMemoBase::database() const {
  return impl->database;
}

const CanonicalQueryKey& RevisionLocalCapabilityMemoBase::key() const { return impl->key; }

DatabaseRevision RevisionLocalCapabilityMemoBase::revision() const noexcept {
  return impl->revision;
}

const SnapshotCapabilityArena& RevisionLocalCapabilityMemoBase::arena() const {
  return *impl->arena.get();
}

zc::ArrayPtr<const zc::Arc<RevisionLocalCapabilityMemoBase>>
RevisionLocalCapabilityMemoBase::retainedDependencies() const {
  return impl->retainedDependencies.asPtr();
}

zc::ArrayPtr<const uint8_t> RevisionLocalCapabilityMemoBase::stableWitness() const {
  return impl->stableWitness.asPtr();
}

DependencyRecord::DependencyRecord(CanonicalQueryKey&& key, DatabaseRevision changedAt,
                                   Durability durability,
                                   zc::Maybe<InputProbeObservation> inputProbeObservation) noexcept
    : keyField(zc::mv(key)),
      changedAtField(changedAt),
      durabilityField(durability),
      inputProbeObservationField(inputProbeObservation),
      stableWitnessField(zc::none) {}

DependencyRecord DependencyRecord::clone() const {
  ZC_IF_SOME(witness, stableWitnessField) {
    return revisionLocalCapability(keyField.clone(), changedAtField, durabilityField,
                                   witness.asPtr());
  }
  return DependencyRecord(keyField.clone(), changedAtField, durabilityField,
                          inputProbeObservationField);
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> DependencyRecord::stableWitness() const noexcept {
  ZC_IF_SOME(witness, stableWitnessField) { return witness.asPtr(); }
  return zc::none;
}

DependencyRecord DependencyRecord::revisionLocalCapability(
    CanonicalQueryKey&& key, DatabaseRevision changedAt, Durability durability,
    zc::ArrayPtr<const uint8_t> stableWitness) {
  DependencyRecord result(zc::mv(key), changedAt, durability);
  result.stableWitnessField = zc::heapArray<uint8_t>(stableWitness);
  return result;
}

DependencyGroup::DependencyGroup(Kind kind, zc::Vector<DependencyRecord>&& dependencies) noexcept
    : kindField(kind), dependencyFields(zc::mv(dependencies)) {}

DependencyGroup DependencyGroup::sequential(DependencyRecord&& dependency) {
  zc::Vector<DependencyRecord> dependencies;
  dependencies.add(zc::mv(dependency));
  return DependencyGroup(Kind::Sequential, zc::mv(dependencies));
}

DependencyGroup DependencyGroup::parallel(zc::Vector<DependencyRecord>&& dependencies) {
  for (size_t index = 1; index < dependencies.size(); ++index) {
    size_t position = index;
    while (position > 0 && dependencies[position].key() < dependencies[position - 1].key()) {
      auto temporary = zc::mv(dependencies[position]);
      dependencies[position] = zc::mv(dependencies[position - 1]);
      dependencies[position - 1] = zc::mv(temporary);
      --position;
    }
  }
  return DependencyGroup(Kind::Parallel, zc::mv(dependencies));
}

DependencyGroup DependencyGroup::clone() const {
  zc::Vector<DependencyRecord> dependencies;
  for (const auto& dependency : dependencyFields) { dependencies.add(dependency.clone()); }
  return DependencyGroup(kindField, zc::mv(dependencies));
}

QueryEvent::QueryEvent(DatabaseRevision revision, CanonicalQueryKey&& key,
                       QueryEventKind kind) noexcept
    : revisionField(revision), keyField(zc::mv(key)), kindField(kind) {}

QueryEvent QueryEvent::clone() const {
  return QueryEvent(revisionField, keyField.clone(), kindField);
}

}  // namespace zomlang::compiler::query
