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

QueryRequestResult::QueryRequestResult(zc::Maybe<QueryValue>&& value,
                                       QueryRuntimeFailure failure) noexcept
    : valueField(zc::mv(value)), failureField(failure) {}

QueryRequestResult QueryRequestResult::completed(QueryValue&& value) {
  zc::Maybe<QueryValue> retained(zc::mv(value));
  return QueryRequestResult(zc::mv(retained), QueryRuntimeFailure::InvariantViolation);
}

QueryRequestResult QueryRequestResult::failed(QueryRuntimeFailure failure) {
  zc::Maybe<QueryValue> noValue;
  return QueryRequestResult(zc::mv(noValue), failure);
}

QueryRequestResult QueryRequestResult::clone() const {
  ZC_IF_SOME(value, valueField) { return completed(value.clone()); }
  return failed(failureField);
}

const QueryValue& QueryRequestResult::value() const {
  ZC_IREQUIRE(valueField != zc::none, "query request has no completed value");
  return ZC_REQUIRE_NONNULL(valueField);
}

DependencyRecord::DependencyRecord(CanonicalQueryKey&& key, DatabaseRevision changedAt,
                                   Durability durability,
                                   zc::Maybe<InputProbeObservation> inputProbeObservation) noexcept
    : keyField(zc::mv(key)),
      changedAtField(changedAt),
      durabilityField(durability),
      inputProbeObservationField(inputProbeObservation) {}

DependencyRecord DependencyRecord::clone() const {
  return DependencyRecord(keyField.clone(), changedAtField, durabilityField,
                          inputProbeObservationField);
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
