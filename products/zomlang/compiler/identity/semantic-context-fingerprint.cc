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

#include "zomlang/compiler/identity/semantic-context-fingerprint.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

constexpr char kFingerprintDomain[] = "zom.semantic-context";
constexpr char kCoreFingerprintDomain[] = "zom.core-semantic-context";

struct EncodedValue final {
  explicit EncodedValue(zc::Array<uint8_t>&& bytes) noexcept : value(zc::mv(bytes)) {}
  EncodedValue(EncodedValue&&) noexcept = default;
  EncodedValue& operator=(EncodedValue&&) noexcept = default;
  ZC_DISALLOW_COPY(EncodedValue);

  zc::Array<uint8_t> value;
};

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t sharedSize = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < sharedSize; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  return left == right;
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (uint32_t shift = 56;; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> shift));
    if (shift == 0) { break; }
  }
}

template <typename Value>
bool appendSortedSequence(zc::Vector<uint8_t>& output, zc::ArrayPtr<const Value> values) {
  zc::Vector<EncodedValue> encoded(values.size());
  for (const auto& value : values) { encoded.add(EncodedValue(value.encode())); }

  for (size_t index = 1; index < encoded.size(); ++index) {
    auto current = zc::mv(encoded[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessBytes(current.value.asPtr(), encoded[insertion - 1].value.asPtr())) {
      encoded[insertion] = zc::mv(encoded[insertion - 1]);
      --insertion;
    }
    encoded[insertion] = zc::mv(current);
  }

  for (size_t index = 1; index < encoded.size(); ++index) {
    if (sameBytes(encoded[index - 1].value.asPtr(), encoded[index].value.asPtr())) { return false; }
  }

  appendUint64(output, encoded.size());
  for (const auto& value : encoded) { output.addAll(value.value); }
  return true;
}

}  // namespace

SourceContentIdentity::SourceContentIdentity(SourceFileKey&& source,
                                             const Sha256Digest& contentDigest) noexcept
    : sourceValue(zc::mv(source)), digestValue(contentDigest) {}

SourceContentIdentity SourceContentIdentity::from(const ImmutableSourceSnapshot& snapshot) {
  return SourceContentIdentity(snapshot.source().clone(), snapshot.contentDigest());
}

bool SourceContentIdentity::sameSourceAs(const SourceContentIdentity& other) const {
  return sourceValue.sameAs(other.sourceValue);
}

zc::Array<uint8_t> SourceContentIdentity::encode() const {
  CanonicalEncoder encoder;
  sourceValue.encode(encoder);
  encoder.encodeDigest(digestValue);
  return encoder.finish();
}

ToolchainSemanticContextInput::ToolchainSemanticContextInput(
    ToolchainUnitKey toolchain, const Sha256Digest& distributionDigest,
    const Sha256Digest& policyTemplateRevision) noexcept
    : toolchainValue(toolchain),
      distributionDigestValue(distributionDigest),
      policyTemplateRevisionValue(policyTemplateRevision) {}

ToolchainSemanticContextInput ToolchainSemanticContextInput::from(
    ToolchainUnitKey toolchain, const Sha256Digest& distributionDigest,
    const Sha256Digest& policyTemplateRevision) noexcept {
  return ToolchainSemanticContextInput(toolchain, distributionDigest, policyTemplateRevision);
}

ToolchainSemanticContextInput ToolchainSemanticContextInput::clone() const noexcept {
  return ToolchainSemanticContextInput(toolchainValue, distributionDigestValue,
                                       policyTemplateRevisionValue);
}

const ToolchainUnitKey& ToolchainSemanticContextInput::toolchain() const noexcept {
  return toolchainValue;
}

const Sha256Digest& ToolchainSemanticContextInput::distributionDigest() const noexcept {
  return distributionDigestValue;
}

const Sha256Digest& ToolchainSemanticContextInput::policyTemplateRevision() const noexcept {
  return policyTemplateRevisionValue;
}

zc::Array<uint8_t> ToolchainSemanticContextInput::encode() const {
  CanonicalEncoder encoder;
  toolchainValue.encode(encoder);
  encoder.encodeDigest(distributionDigestValue);
  encoder.encodeDigest(policyTemplateRevisionValue);
  return encoder.finish();
}

CoreSemanticContextFingerprint::CoreSemanticContextFingerprint(const Sha256Digest& digest) noexcept
    : value(digest) {}

zc::Maybe<CoreSemanticContextFingerprint> CoreSemanticContextFingerprint::compute(
    const CrateKey& projectedCoreCrate) {
  if (projectedCoreCrate.unit().kind() != CompilationUnitKind::Toolchain ||
      projectedCoreCrate.unit().toolchain().component() != ToolchainComponent::Core ||
      projectedCoreCrate.targetKind() != CrateTargetKind::Library ||
      projectedCoreCrate.targetName() != "core"_zc ||
      projectedCoreCrate.compilation().hasBuildScriptProducer() ||
      projectedCoreCrate.semanticOptions().editionYear() != 2026) {
    return zc::none;
  }
  const auto crate = projectedCoreCrate.encode();
  zc::Vector<uint8_t> bytes(sizeof(kCoreFingerprintDomain) + crate.size());
  for (size_t index = 0; index < sizeof(kCoreFingerprintDomain) - 1; ++index) {
    bytes.add(static_cast<uint8_t>(kCoreFingerprintDomain[index]));
  }
  bytes.add(0);
  bytes.addAll(crate.asPtr());
  auto digest = sha256(bytes.asPtr());
  ZC_IF_SOME(value, digest) { return CoreSemanticContextFingerprint(value); }
  return zc::none;
}

CoreSemanticContextFingerprint CoreSemanticContextFingerprint::clone() const noexcept {
  return CoreSemanticContextFingerprint(value);
}

const Sha256Digest& CoreSemanticContextFingerprint::digest() const noexcept { return value; }

void CoreSemanticContextFingerprint::encode(CanonicalEncoder& encoder) const {
  encoder.encodeDigest(value);
}

SemanticContextFingerprint::SemanticContextFingerprint(const Sha256Digest& digest) noexcept
    : value(digest) {}

zc::Maybe<SemanticContextFingerprint> SemanticContextFingerprint::compute(
    zc::ArrayPtr<const CompilationUnitIdentity> compilationUnits,
    zc::ArrayPtr<const ToolchainSemanticContextInput> toolchainInputs,
    zc::ArrayPtr<const PackageDependencyEdgeKey> packageEdges, zc::ArrayPtr<const CrateKey> crates,
    zc::ArrayPtr<const CrateDependencyEdgeKey> crateEdges,
    zc::ArrayPtr<const SourceContentIdentity> sourceContents,
    zc::ArrayPtr<const ModuleKey> modules) {
  for (size_t left = 0; left < sourceContents.size(); ++left) {
    for (size_t right = left + 1; right < sourceContents.size(); ++right) {
      if (sourceContents[left].sameSourceAs(sourceContents[right])) { return zc::none; }
    }
  }
  for (const auto& unit : compilationUnits) {
    if (unit.kind() != CompilationUnitKind::Toolchain) { continue; }
    size_t matches = 0;
    for (const auto& input : toolchainInputs) {
      if (input.toolchain().component() == unit.toolchain().component()) { ++matches; }
    }
    if (matches != 1) { return zc::none; }
  }
  for (const auto& input : toolchainInputs) {
    size_t matches = 0;
    for (const auto& unit : compilationUnits) {
      if (unit.kind() == CompilationUnitKind::Toolchain &&
          unit.toolchain().component() == input.toolchain().component()) {
        ++matches;
      }
    }
    if (matches != 1) { return zc::none; }
  }

  zc::Vector<uint8_t> bytes;
  for (size_t index = 0; index < sizeof(kFingerprintDomain) - 1; ++index) {
    bytes.add(static_cast<uint8_t>(kFingerprintDomain[index]));
  }
  bytes.add(0x00);
  if (!appendSortedSequence(bytes, compilationUnits) ||
      !appendSortedSequence(bytes, toolchainInputs) || !appendSortedSequence(bytes, packageEdges) ||
      !appendSortedSequence(bytes, crates) || !appendSortedSequence(bytes, crateEdges) ||
      !appendSortedSequence(bytes, sourceContents) || !appendSortedSequence(bytes, modules)) {
    return zc::none;
  }

  auto digest = sha256(bytes.asPtr());
  ZC_IF_SOME(result, digest) { return SemanticContextFingerprint(result); }
  return zc::none;
}

zc::Maybe<SemanticContextFingerprint> SemanticContextFingerprint::compute(
    const SemanticIdentityRegistrySet& registries,
    zc::ArrayPtr<const ToolchainSemanticContextInput> toolchainInputs,
    zc::ArrayPtr<const PackageDependencyEdgeKey> packageEdges,
    zc::ArrayPtr<const CrateDependencyEdgeKey> crateEdges) {
  if (!registries.compilationUnits().isFrozen() || !registries.crates().isFrozen() ||
      !registries.sourceFiles().isFrozen() || !registries.modules().isFrozen()) {
    return zc::none;
  }

  zc::Vector<CompilationUnitIdentity> compilationUnits(registries.compilationUnits().size());
  for (size_t index = 0; index < registries.compilationUnits().size(); ++index) {
    ZC_IF_SOME(key, registries.compilationUnits().keyAt(index)) {
      compilationUnits.add(key.clone());
    }
  }
  zc::Vector<CrateKey> crates(registries.crates().size());
  for (size_t index = 0; index < registries.crates().size(); ++index) {
    ZC_IF_SOME(key, registries.crates().keyAt(index)) { crates.add(key.clone()); }
  }
  zc::Vector<SourceContentIdentity> sourceContents(registries.sourceSnapshots().size());
  for (const auto& snapshot : registries.sourceSnapshots()) {
    sourceContents.add(SourceContentIdentity::from(snapshot));
  }
  zc::Vector<ModuleKey> modules(registries.modules().size());
  for (size_t index = 0; index < registries.modules().size(); ++index) {
    ZC_IF_SOME(key, registries.modules().keyAt(index)) { modules.add(key.clone()); }
  }

  return compute(compilationUnits.asPtr(), toolchainInputs, packageEdges, crates.asPtr(),
                 crateEdges, sourceContents.asPtr(), modules.asPtr());
}

const Sha256Digest& SemanticContextFingerprint::digest() const noexcept { return value; }

SemanticContextFingerprint SemanticContextFingerprint::clone() const noexcept {
  return SemanticContextFingerprint(value);
}

}  // namespace zomlang::compiler::identity
