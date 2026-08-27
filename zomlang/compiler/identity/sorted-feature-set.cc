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

#include "zomlang/compiler/identity/sorted-feature-set.h"

#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

constexpr uint64_t kMaximumFeatureCount = 256;

template <typename Scalar>
bool encodedLess(const Scalar& left, const Scalar& right) {
  if (left.text().size() != right.text().size()) {
    return left.text().size() < right.text().size();
  }
  return left < right;
}

template <typename Scalar>
void sortCanonical(zc::Vector<Scalar>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion > 0 && encodedLess(current, values[insertion - 1])) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

}  // namespace

SortedFeatureSet::SortedFeatureSet(zc::Vector<FeatureName>&& canonical) noexcept
    : features(zc::mv(canonical)) {}

zc::Maybe<SortedFeatureSet> SortedFeatureSet::from(zc::Vector<FeatureName>&& input) {
  if (input.size() > kMaximumFeatureCount) { return zc::none; }
  sortCanonical(input);
  for (size_t index = 1; index < input.size(); ++index) {
    if (input[index] == input[index - 1]) { return zc::none; }
  }
  return SortedFeatureSet(zc::mv(input));
}

zc::Maybe<SortedFeatureSet> SortedFeatureSet::decodeCanonical(CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(kMaximumFeatureCount);
  if (count == zc::none) { return zc::none; }
  ZC_IF_SOME(size, count) {
    zc::Vector<FeatureName> result(static_cast<size_t>(size));
    for (uint64_t index = 0; index < size; ++index) {
      auto feature = FeatureName::decodeCanonical(decoder);
      if (feature == zc::none) { return zc::none; }
      ZC_IF_SOME(value, feature) {
        if (result.size() != 0 && !encodedLess(result.back(), value)) { return zc::none; }
        result.add(zc::mv(value));
      }
    }
    return SortedFeatureSet(zc::mv(result));
  }
  return zc::none;
}

SortedFeatureSet SortedFeatureSet::clone() const {
  zc::Vector<FeatureName> result(features.size());
  for (const auto& feature : features) { result.add(feature.clone()); }
  return SortedFeatureSet(zc::mv(result));
}

SortedFeatureSet SortedFeatureSet::clone(zc::MemoryResource& resource) const {
  zc::Vector<FeatureName> result(resource, features.size());
  for (const auto& feature : features) { result.add(feature.clone(resource)); }
  return SortedFeatureSet(zc::mv(result));
}

zc::ArrayPtr<const FeatureName> SortedFeatureSet::values() const noexcept {
  return features.asPtr();
}

void SortedFeatureSet::encode(CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(features.size());
  for (const auto& feature : features) { feature.encode(encoder); }
}

SortedTargetFeatureSet::SortedTargetFeatureSet(zc::Vector<TargetFeatureName>&& canonical) noexcept
    : features(zc::mv(canonical)) {}

zc::Maybe<SortedTargetFeatureSet> SortedTargetFeatureSet::from(
    zc::Vector<TargetFeatureName>&& input) {
  if (input.size() > kMaximumFeatureCount) { return zc::none; }
  sortCanonical(input);
  for (size_t index = 1; index < input.size(); ++index) {
    if (input[index] == input[index - 1]) { return zc::none; }
  }
  return SortedTargetFeatureSet(zc::mv(input));
}

zc::Maybe<SortedTargetFeatureSet> SortedTargetFeatureSet::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(kMaximumFeatureCount);
  if (count == zc::none) { return zc::none; }
  ZC_IF_SOME(size, count) {
    zc::Vector<TargetFeatureName> result(static_cast<size_t>(size));
    for (uint64_t index = 0; index < size; ++index) {
      auto feature = TargetFeatureName::decodeCanonical(decoder);
      if (feature == zc::none) { return zc::none; }
      ZC_IF_SOME(value, feature) {
        if (result.size() != 0 && !encodedLess(result.back(), value)) { return zc::none; }
        result.add(zc::mv(value));
      }
    }
    return SortedTargetFeatureSet(zc::mv(result));
  }
  return zc::none;
}

SortedTargetFeatureSet SortedTargetFeatureSet::clone() const {
  zc::Vector<TargetFeatureName> result(features.size());
  for (const auto& feature : features) { result.add(feature.clone()); }
  return SortedTargetFeatureSet(zc::mv(result));
}

zc::ArrayPtr<const TargetFeatureName> SortedTargetFeatureSet::values() const noexcept {
  return features.asPtr();
}

void SortedTargetFeatureSet::encode(CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(features.size());
  for (const auto& feature : features) { feature.encode(encoder); }
}

}  // namespace zomlang::compiler::identity
