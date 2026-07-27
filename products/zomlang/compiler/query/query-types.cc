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

#include "zomlang/compiler/query/query-types.h"

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

}  // namespace

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

struct SemanticContextCapabilityArena::Impl final {
  Impl() = default;
  explicit Impl(zc::Own<SemanticContextCapabilityResources>&& resources) noexcept
      : resources(zc::mv(resources)) {}

  zc::Own<SemanticContextCapabilityResources> resources;
};

SemanticContextCapabilityArena::SemanticContextCapabilityArena() : impl(zc::heap<Impl>()) {}
SemanticContextCapabilityArena::SemanticContextCapabilityArena(
    zc::Own<SemanticContextCapabilityResources>&& resources)
    : impl(zc::heap<Impl>(zc::mv(resources))) {
  ZC_IREQUIRE(impl->resources.get() != nullptr,
              "semantic context capability arena has no resources");
}
SemanticContextCapabilityArena::~SemanticContextCapabilityArena() noexcept(false) = default;

bool SemanticContextCapabilityArena::hasResources() const noexcept {
  return impl->resources.get() != nullptr;
}

const SemanticContextCapabilityResources& SemanticContextCapabilityArena::resources() const {
  ZC_IREQUIRE(impl->resources.get() != nullptr,
              "semantic context capability arena has no resources");
  return *impl->resources;
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
  Impl(CanonicalQueryKey&& key, DatabaseRevision revision, zc::Arc<SnapshotCapabilityArena>&& arena,
       zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies,
       zc::Array<uint8_t>&& stableWitness, zc::StringPtr capabilityTypeIdentity) noexcept
      : key(zc::mv(key)),
        revision(revision),
        arena(zc::mv(arena)),
        retainedDependencies(zc::mv(retainedDependencies)),
        stableWitness(zc::mv(stableWitness)),
        capabilityTypeIdentity(zc::str(capabilityTypeIdentity)) {}

  CanonicalQueryKey key;
  DatabaseRevision revision;
  zc::Arc<SnapshotCapabilityArena> arena;
  zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>> retainedDependencies;
  zc::Array<uint8_t> stableWitness;
  zc::String capabilityTypeIdentity;
};

RevisionLocalCapabilityMemoBase::RevisionLocalCapabilityMemoBase(
    CanonicalQueryKey&& key, DatabaseRevision revision, zc::Arc<SnapshotCapabilityArena>&& arena,
    zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies,
    zc::Array<uint8_t>&& stableWitness, zc::StringPtr capabilityTypeIdentity)
    : impl(zc::heap<Impl>(zc::mv(key), revision, zc::mv(arena), zc::mv(retainedDependencies),
                          zc::mv(stableWitness), capabilityTypeIdentity)) {}

RevisionLocalCapabilityMemoBase::~RevisionLocalCapabilityMemoBase() noexcept(false) = default;

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

zc::StringPtr RevisionLocalCapabilityMemoBase::capabilityTypeIdentity() const {
  return impl->capabilityTypeIdentity;
}

QueryRequestResult::QueryRequestResult(zc::Maybe<QueryValue>&& value,
                                       zc::Arc<RevisionLocalCapabilityMemoBase>&& capabilityMemo,
                                       QueryRuntimeFailure failure) noexcept
    : valueField(zc::mv(value)),
      capabilityMemoField(zc::mv(capabilityMemo)),
      failureField(failure) {}

QueryRequestResult QueryRequestResult::completed(QueryValue&& value) {
  zc::Maybe<QueryValue> retained(zc::mv(value));
  zc::Arc<RevisionLocalCapabilityMemoBase> noCapability;
  return QueryRequestResult(zc::mv(retained), zc::mv(noCapability),
                            QueryRuntimeFailure::InvariantViolation);
}

QueryRequestResult QueryRequestResult::completed(
    zc::Arc<RevisionLocalCapabilityMemoBase>&& capabilityMemo) {
  ZC_IREQUIRE(capabilityMemo != nullptr, "query capability completion has no memo");
  zc::Maybe<QueryValue> noValue;
  return QueryRequestResult(zc::mv(noValue), zc::mv(capabilityMemo),
                            QueryRuntimeFailure::InvariantViolation);
}

QueryRequestResult QueryRequestResult::failed(QueryRuntimeFailure failure) {
  zc::Maybe<QueryValue> noValue;
  zc::Arc<RevisionLocalCapabilityMemoBase> noCapability;
  return QueryRequestResult(zc::mv(noValue), zc::mv(noCapability), failure);
}

QueryRequestResult QueryRequestResult::clone() const {
  ZC_IF_SOME(value, valueField) { return completed(value.clone()); }
  if (capabilityMemoField != nullptr) { return completed(capabilityMemoField.addRef()); }
  return failed(failureField);
}

const QueryValue& QueryRequestResult::value() const {
  ZC_IREQUIRE(valueField != zc::none, "query request has no canonical completed value");
  return ZC_REQUIRE_NONNULL(valueField);
}

const RevisionLocalCapabilityMemoBase& QueryRequestResult::capabilityMemo() const {
  ZC_IREQUIRE(capabilityMemoField != nullptr, "query request has no capability completion");
  return *capabilityMemoField.get();
}

zc::Arc<RevisionLocalCapabilityMemoBase> QueryRequestResult::capabilityMemoArc() const {
  ZC_IREQUIRE(capabilityMemoField != nullptr, "query request has no capability completion");
  return capabilityMemoField.addRef();
}

zc::Arc<RevisionLocalCapabilityMemoBase> QueryRequestResult::takeCapabilityMemo() {
  ZC_IREQUIRE(capabilityMemoField != nullptr, "query request has no capability completion");
  return zc::mv(capabilityMemoField);
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
