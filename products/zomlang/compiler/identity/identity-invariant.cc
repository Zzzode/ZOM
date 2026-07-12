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

#include "zomlang/compiler/identity/identity-invariant.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::identity {
namespace {

bool validPhase(IdentityAllocationPhase value) noexcept {
  return value >= IdentityAllocationPhase::Context &&
         value <= IdentityAllocationPhase::SemanticType;
}

bool validKind(IdentityInvariantKind value) noexcept {
  return value >= IdentityInvariantKind::InvalidHandle &&
         value <= IdentityInvariantKind::NonCanonicalEncoding;
}

bool validApiSite(IdentityApiSite value) noexcept {
  return value >= IdentityApiSite::ContextBrandIssue && value <= IdentityApiSite::RegistryMutation;
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t sharedSize = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < sharedSize; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

int compareOptionalBytes(zc::Maybe<zc::ArrayPtr<const uint8_t>> left,
                         zc::Maybe<zc::ArrayPtr<const uint8_t>> right) {
  if (left == zc::none) { return right == zc::none ? 0 : -1; }
  if (right == zc::none) { return 1; }
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) { return compareBytes(leftValue, rightValue); }
  }
  ZC_UNREACHABLE
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> encodeRange(zc::Maybe<const UnbrandedSourceRange&> range,
                                                   zc::Array<uint8_t>& storage) {
  ZC_IF_SOME(value, range) {
    storage = value.encode();
    zc::ArrayPtr<const uint8_t> bytes = storage.asPtr();
    return bytes;
  }
  return zc::none;
}

bool invariantLess(const IdentityInvariant& left, const IdentityInvariant& right) {
  if (left.phase() != right.phase()) { return left.phase() < right.phase(); }
  if (left.kind() != right.kind()) { return left.kind() < right.kind(); }
  const int structuralOrder =
      compareOptionalBytes(left.structuralInputKey(), right.structuralInputKey());
  if (structuralOrder != 0) { return structuralOrder < 0; }

  zc::Array<uint8_t> leftRangeBytes;
  zc::Array<uint8_t> rightRangeBytes;
  const int rangeOrder =
      compareOptionalBytes(encodeRange(left.diagnosticRange(), leftRangeBytes),
                           encodeRange(right.diagnosticRange(), rightRangeBytes));
  if (rangeOrder != 0) { return rangeOrder < 0; }
  if (left.apiSite() != right.apiSite()) { return left.apiSite() < right.apiSite(); }
  return left.inputTraversalOrdinal() < right.inputTraversalOrdinal();
}

}  // namespace

IdentityInvariant::IdentityInvariant(IdentityInvariantKind kind, IdentityAllocationPhase phase,
                                     zc::Maybe<zc::Array<uint8_t>>&& structuralInputKey,
                                     zc::Maybe<UnbrandedSourceRange>&& diagnosticRange,
                                     IdentityApiSite apiSite,
                                     uint32_t inputTraversalOrdinal) noexcept
    : kindValue(kind),
      phaseValue(phase),
      structuralInputKeyValue(zc::mv(structuralInputKey)),
      diagnosticRangeValue(zc::mv(diagnosticRange)),
      apiSiteValue(apiSite),
      traversalOrdinalValue(inputTraversalOrdinal) {}

zc::Maybe<IdentityInvariant> IdentityInvariant::from(
    IdentityInvariantKind kind, IdentityAllocationPhase phase,
    zc::Maybe<zc::Array<uint8_t>>&& structuralInputKey,
    zc::Maybe<UnbrandedSourceRange>&& diagnosticRange, IdentityApiSite apiSite,
    uint32_t inputTraversalOrdinal) {
  if (!validKind(kind) || !validPhase(phase) || !validApiSite(apiSite)) { return zc::none; }
  return IdentityInvariant(kind, phase, zc::mv(structuralInputKey), zc::mv(diagnosticRange),
                           apiSite, inputTraversalOrdinal);
}

IdentityInvariant IdentityInvariant::clone() const {
  zc::Maybe<zc::Array<uint8_t>> structural;
  ZC_IF_SOME(value, structuralInputKeyValue) { structural = zc::heapArray(value.asPtr()); }
  zc::Maybe<UnbrandedSourceRange> range;
  ZC_IF_SOME(value, diagnosticRangeValue) { range = value.clone(); }
  return IdentityInvariant(kindValue, phaseValue, zc::mv(structural), zc::mv(range), apiSiteValue,
                           traversalOrdinalValue);
}

IdentityInvariantKind IdentityInvariant::kind() const noexcept { return kindValue; }
IdentityAllocationPhase IdentityInvariant::phase() const noexcept { return phaseValue; }
zc::Maybe<zc::ArrayPtr<const uint8_t>> IdentityInvariant::structuralInputKey() const {
  ZC_IF_SOME(value, structuralInputKeyValue) { return value.asPtr(); }
  return zc::none;
}
zc::Maybe<const UnbrandedSourceRange&> IdentityInvariant::diagnosticRange() const {
  ZC_IF_SOME(value, diagnosticRangeValue) { return value; }
  return zc::none;
}
IdentityApiSite IdentityInvariant::apiSite() const noexcept { return apiSiteValue; }
uint32_t IdentityInvariant::inputTraversalOrdinal() const noexcept { return traversalOrdinalValue; }

struct IdentityInvariantCollector::Impl final {
  zc::Vector<IdentityInvariant> values;
};

IdentityInvariantCollector::IdentityInvariantCollector() noexcept : impl(zc::heap<Impl>()) {}
IdentityInvariantCollector::~IdentityInvariantCollector() noexcept(false) = default;
IdentityInvariantCollector::IdentityInvariantCollector(IdentityInvariantCollector&&) noexcept =
    default;
IdentityInvariantCollector& IdentityInvariantCollector::operator=(
    IdentityInvariantCollector&&) noexcept = default;

void IdentityInvariantCollector::add(IdentityInvariant&& invariant) {
  impl->values.add(zc::mv(invariant));
}

void IdentityInvariantCollector::sort() {
  for (size_t index = 1; index < impl->values.size(); ++index) {
    auto current = zc::mv(impl->values[index]);
    size_t insertion = index;
    while (insertion > 0 && invariantLess(current, impl->values[insertion - 1])) {
      impl->values[insertion] = zc::mv(impl->values[insertion - 1]);
      --insertion;
    }
    impl->values[insertion] = zc::mv(current);
  }
}

zc::ArrayPtr<const IdentityInvariant> IdentityInvariantCollector::facts() const noexcept {
  return impl->values.asPtr();
}

}  // namespace zomlang::compiler::identity
