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

#include "zomlang/compiler/irgen/target-registry.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::irgen {
namespace {

bool isAscii(zc::StringPtr value) {
  if (value.size() == 0) { return false; }
  for (const auto byte : value) {
    if (static_cast<uint8_t>(byte) >= 0x80U || byte == '\0') { return false; }
  }
  return true;
}

void append(zc::Vector<uint8_t>& output, zc::StringPtr value) {
  for (const auto byte : value) { output.add(static_cast<uint8_t>(byte)); }
}

void append(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  output.addAll(value);
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

void appendFramed(zc::Vector<uint8_t>& output, zc::StringPtr value) {
  appendUint64(output, value.size());
  append(output, value);
}

void appendFramed(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  appendUint64(output, value.size());
  append(output, value);
}

identity::Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE;
}

identity::Sha256Digest computeTargetSpecId(zc::StringPtr triple, zc::StringPtr dataLayout,
                                           zc::StringPtr cpu,
                                           zc::ArrayPtr<const CanonicalTargetFeature> features,
                                           zc::StringPtr runtimeAbi, BackendPanicStrategy panic,
                                           ObjectFormat object) {
  zc::Vector<uint8_t> preimage;
  append(preimage, "zom.target-spec.v1"_zc);
  preimage.add(0);
  appendFramed(preimage, triple);
  appendFramed(preimage, dataLayout);
  appendFramed(preimage, cpu);
  appendUint64(preimage, features.size());
  for (const auto& feature : features) {
    appendFramed(preimage, feature.name());
    preimage.add(static_cast<uint8_t>(feature.state()));
  }
  appendFramed(preimage, runtimeAbi);
  preimage.add(static_cast<uint8_t>(panic));
  preimage.add(static_cast<uint8_t>(object));
  return requireDigest(preimage.asPtr());
}

template <typename Value, typename Less>
void insertionSort(zc::Vector<Value>& values, Less&& less) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && less(current, values[insertion - 1])) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

zc::Maybe<identity::CanonicalTargetSpecificationKey> project(
    const CanonicalTargetSpec& spec,
    zc::ArrayPtr<const identity::TargetFeatureName> semanticFeatures) {
  zc::Vector<zc::String> tripleComponents;
  size_t begin = 0;
  while (begin <= spec.triple().size()) {
    size_t end = begin;
    while (end < spec.triple().size() && spec.triple()[end] != '-') { ++end; }
    if (end == begin) { return zc::none; }
    tripleComponents.add(zc::str(spec.triple().slice(begin, end)));
    if (end == spec.triple().size()) { break; }
    begin = end + 1;
  }
  if (tripleComponents.size() < 3 || tripleComponents.size() > 4) { return zc::none; }
  uint32_t pointerWidth = 0;
  identity::Endianness endianness;
  if (spec.llvmDataLayout() == "e-p:32:32"_zc) {
    pointerWidth = 32;
    endianness = identity::Endianness::Little;
  } else if (spec.llvmDataLayout() == "e-p:64:64"_zc) {
    pointerWidth = 64;
    endianness = identity::Endianness::Little;
  } else if (spec.llvmDataLayout() == "E-p:32:32"_zc) {
    pointerWidth = 32;
    endianness = identity::Endianness::Big;
  } else if (spec.llvmDataLayout() == "E-p:64:64"_zc) {
    pointerWidth = 64;
    endianness = identity::Endianness::Big;
  } else {
    return zc::none;
  }
  auto architecture = identity::TargetComponentName::fromCanonical(tripleComponents[0]);
  auto vendor = identity::TargetComponentName::fromCanonical(tripleComponents[1]);
  auto operatingSystem = identity::TargetComponentName::fromCanonical(tripleComponents[2]);
  auto environment = identity::TargetComponentName::fromCanonical(
      tripleComponents.size() == 4 ? tripleComponents[3] : "unknown"_zc);
  auto abi = identity::TargetComponentName::fromCanonical(spec.runtimeAbiProfile());
  if (architecture == zc::none || vendor == zc::none || operatingSystem == zc::none ||
      environment == zc::none || abi == zc::none) {
    return zc::none;
  }
  zc::Vector<identity::TargetFeatureName> projected;
  for (const auto& requested : semanticFeatures) {
    bool found = false;
    for (const auto& feature : spec.features()) {
      if (feature.name() == requested.text() && feature.state() == TargetFeatureState::Enabled) {
        found = true;
        break;
      }
    }
    if (!found) { return zc::none; }
    projected.add(requested.clone());
  }
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(projected));
  if (sorted == zc::none) { return zc::none; }
  ZC_IF_SOME(architectureValue, architecture) {
    ZC_IF_SOME(vendorValue, vendor) {
      ZC_IF_SOME(osValue, operatingSystem) {
        ZC_IF_SOME(environmentValue, environment) {
          ZC_IF_SOME(abiValue, abi) {
            ZC_IF_SOME(featureValues, sorted) {
              return identity::CanonicalTargetSpecificationKey::from(
                  zc::mv(architectureValue), zc::mv(vendorValue), zc::mv(osValue),
                  zc::mv(environmentValue), zc::mv(abiValue), pointerWidth, endianness,
                  zc::mv(featureValues));
            }
          }
        }
      }
    }
  }
  ZC_UNREACHABLE;
}

zc::Array<uint8_t> encodeProfileRevision(const RegisteredTargetProfileRecord& profile) {
  zc::Vector<uint8_t> output;
  appendFramed(output, profile.name());
  identity::CanonicalEncoder projectionEncoder;
  profile.semanticProjection().encode(projectionEncoder);
  append(output, projectionEncoder.finish().asPtr());
  appendUint64(output, profile.semanticFeatures().size());
  for (const auto& feature : profile.semanticFeatures()) { appendFramed(output, feature.text()); }
  appendUint64(output, profile.specifications().size());
  for (const auto& specification : profile.specifications()) {
    output.add(static_cast<uint8_t>(specification.panicStrategy()));
    append(output, specification.targetSpecId().bytes());
  }
  return output.releaseAsArray();
}

bool sameProjection(const identity::CanonicalTargetSpecificationKey& left,
                    const identity::CanonicalTargetSpecificationKey& right) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  return leftEncoder.finish().asPtr() == rightEncoder.finish().asPtr();
}

}  // namespace

CanonicalTargetFeature::CanonicalTargetFeature(zc::String&& name, TargetFeatureState state) noexcept
    : nameValue(zc::mv(name)), stateValue(state) {}

zc::Maybe<CanonicalTargetFeature> CanonicalTargetFeature::from(zc::StringPtr name,
                                                               TargetFeatureState state) {
  if (!isAscii(name) ||
      (state != TargetFeatureState::Enabled && state != TargetFeatureState::Disabled)) {
    return zc::none;
  }
  return CanonicalTargetFeature(zc::str(name), state);
}

CanonicalTargetFeature CanonicalTargetFeature::clone() const {
  return CanonicalTargetFeature(zc::str(nameValue), stateValue);
}

zc::StringPtr CanonicalTargetFeature::name() const noexcept { return nameValue; }
TargetFeatureState CanonicalTargetFeature::state() const noexcept { return stateValue; }

CanonicalTargetSpec::CanonicalTargetSpec(zc::String&& triple, zc::String&& dataLayout,
                                         zc::String&& cpu,
                                         zc::Vector<CanonicalTargetFeature>&& features,
                                         zc::String&& runtimeAbi, BackendPanicStrategy panic,
                                         ObjectFormat object,
                                         const identity::Sha256Digest& id) noexcept
    : tripleValue(zc::mv(triple)),
      dataLayoutValue(zc::mv(dataLayout)),
      cpuValue(zc::mv(cpu)),
      featureValues(zc::mv(features)),
      runtimeAbiValue(zc::mv(runtimeAbi)),
      panicValue(panic),
      objectValue(object),
      idValue(id) {}

zc::Maybe<CanonicalTargetSpec> CanonicalTargetSpec::from(
    zc::StringPtr triple, zc::StringPtr llvmDataLayout, zc::StringPtr cpu,
    zc::Vector<CanonicalTargetFeature>&& features, zc::StringPtr runtimeAbiProfile,
    BackendPanicStrategy panicStrategy, ObjectFormat objectFormat) {
  if (!isAscii(triple) || !isAscii(llvmDataLayout) || !isAscii(cpu) ||
      !isAscii(runtimeAbiProfile) ||
      (panicStrategy != BackendPanicStrategy::Unwind &&
       panicStrategy != BackendPanicStrategy::Abort) ||
      (objectFormat != ObjectFormat::Elf && objectFormat != ObjectFormat::MachO &&
       objectFormat != ObjectFormat::Coff && objectFormat != ObjectFormat::Wasm)) {
    return zc::none;
  }
  insertionSort(features,
                [](const auto& left, const auto& right) { return left.name() < right.name(); });
  for (size_t index = 1; index < features.size(); ++index) {
    if (features[index - 1].name() == features[index].name()) { return zc::none; }
  }
  auto id = computeTargetSpecId(triple, llvmDataLayout, cpu, features, runtimeAbiProfile,
                                panicStrategy, objectFormat);
  return CanonicalTargetSpec(zc::str(triple), zc::str(llvmDataLayout), zc::str(cpu),
                             zc::mv(features), zc::str(runtimeAbiProfile), panicStrategy,
                             objectFormat, id);
}

CanonicalTargetSpec CanonicalTargetSpec::clone() const {
  zc::Vector<CanonicalTargetFeature> features(featureValues.size());
  for (const auto& feature : featureValues) { features.add(feature.clone()); }
  return CanonicalTargetSpec(zc::str(tripleValue), zc::str(dataLayoutValue), zc::str(cpuValue),
                             zc::mv(features), zc::str(runtimeAbiValue), panicValue, objectValue,
                             idValue);
}

zc::StringPtr CanonicalTargetSpec::triple() const noexcept { return tripleValue; }
zc::StringPtr CanonicalTargetSpec::llvmDataLayout() const noexcept { return dataLayoutValue; }
zc::StringPtr CanonicalTargetSpec::cpu() const noexcept { return cpuValue; }
zc::ArrayPtr<const CanonicalTargetFeature> CanonicalTargetSpec::features() const noexcept {
  return featureValues;
}
zc::StringPtr CanonicalTargetSpec::runtimeAbiProfile() const noexcept { return runtimeAbiValue; }
BackendPanicStrategy CanonicalTargetSpec::panicStrategy() const noexcept { return panicValue; }
ObjectFormat CanonicalTargetSpec::objectFormat() const noexcept { return objectValue; }
const identity::Sha256Digest& CanonicalTargetSpec::targetSpecId() const noexcept { return idValue; }

RegisteredTargetProfileRecord::RegisteredTargetProfileRecord(
    driver::package::RegisteredTargetProfileName&& name,
    identity::CanonicalTargetSpecificationKey&& semanticProjection,
    zc::Vector<identity::TargetFeatureName>&& semanticFeatures,
    zc::Vector<CanonicalTargetSpec>&& specifications) noexcept
    : nameValue(zc::mv(name)),
      projectionValue(zc::mv(semanticProjection)),
      semanticFeatureValues(zc::mv(semanticFeatures)),
      specificationValues(zc::mv(specifications)) {}

zc::StringPtr RegisteredTargetProfileRecord::name() const noexcept { return nameValue.text(); }

const identity::CanonicalTargetSpecificationKey& RegisteredTargetProfileRecord::semanticProjection()
    const noexcept {
  return projectionValue;
}

zc::ArrayPtr<const identity::TargetFeatureName> RegisteredTargetProfileRecord::semanticFeatures()
    const noexcept {
  return semanticFeatureValues;
}

zc::ArrayPtr<const CanonicalTargetSpec> RegisteredTargetProfileRecord::specifications()
    const noexcept {
  return specificationValues;
}

zc::Maybe<RegisteredTargetProfileRecord> RegisteredTargetProfileRecord::from(
    driver::package::RegisteredTargetProfileName&& name,
    identity::CanonicalTargetSpecificationKey&& semanticProjection,
    zc::Vector<identity::TargetFeatureName>&& semanticFeatures,
    zc::Vector<CanonicalTargetSpec>&& specifications) {
  insertionSort(semanticFeatures,
                [](const auto& left, const auto& right) { return left.text() < right.text(); });
  for (size_t index = 1; index < semanticFeatures.size(); ++index) {
    if (semanticFeatures[index - 1] == semanticFeatures[index]) { return zc::none; }
  }
  insertionSort(specifications, [](const auto& left, const auto& right) {
    return static_cast<uint8_t>(left.panicStrategy()) < static_cast<uint8_t>(right.panicStrategy());
  });
  if (specifications.size() == 0) { return zc::none; }
  for (size_t index = 0; index < specifications.size(); ++index) {
    if (index != 0 &&
        specifications[index - 1].panicStrategy() == specifications[index].panicStrategy()) {
      return zc::none;
    }
    auto projected = project(specifications[index], semanticFeatures);
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(value, projected) {
      if (!sameProjection(value, semanticProjection)) { return zc::none; }
    }
  }
  return RegisteredTargetProfileRecord(zc::mv(name), zc::mv(semanticProjection),
                                       zc::mv(semanticFeatures), zc::mv(specifications));
}

VerifiedTargetSelection::VerifiedTargetSelection(
    driver::package::RegisteredTargetSelection&& packageSelection,
    CanonicalTargetSpec&& spec) noexcept
    : packageSelectionValue(zc::mv(packageSelection)), specificationValue(zc::mv(spec)) {}

const driver::package::RegisteredTargetSelection& VerifiedTargetSelection::packageSelection()
    const noexcept {
  return packageSelectionValue;
}

const CanonicalTargetSpec& VerifiedTargetSelection::targetSpec() const noexcept {
  return specificationValue;
}

const identity::Sha256Digest& VerifiedTargetSelection::targetSpecId() const noexcept {
  return specificationValue.targetSpecId();
}

TargetRegistrySnapshot::TargetRegistrySnapshot(
    driver::package::RegisteredTargetProfileName&& hostProfile,
    zc::Vector<RegisteredTargetProfileRecord>&& profiles,
    const identity::Sha256Digest& revision) noexcept
    : hostProfileValue(zc::mv(hostProfile)),
      profileValues(zc::mv(profiles)),
      revisionValue(revision) {}

zc::Maybe<TargetRegistrySnapshot> TargetRegistrySnapshot::from(
    driver::package::RegisteredTargetProfileName&& hostProfile,
    zc::Vector<RegisteredTargetProfileRecord>&& profiles) {
  if (profiles.size() == 0) { return zc::none; }
  insertionSort(profiles, [](const auto& left, const auto& right) {
    return left.nameValue.text() < right.nameValue.text();
  });
  bool foundHost = false;
  zc::Vector<uint8_t> preimage;
  append(preimage, "zom.target-registry.v0"_zc);
  preimage.add(0);
  appendFramed(preimage, hostProfile.text());
  appendUint64(preimage, profiles.size());
  for (size_t index = 0; index < profiles.size(); ++index) {
    if (index != 0 && profiles[index - 1].nameValue.text() == profiles[index].nameValue.text()) {
      return zc::none;
    }
    if (profiles[index].nameValue.text() == hostProfile.text()) { foundHost = true; }
    appendFramed(preimage, encodeProfileRevision(profiles[index]).asPtr());
  }
  if (!foundHost) { return zc::none; }
  return TargetRegistrySnapshot(zc::mv(hostProfile), zc::mv(profiles),
                                requireDigest(preimage.asPtr()));
}

const identity::Sha256Digest& TargetRegistrySnapshot::revision() const noexcept {
  return revisionValue;
}

zc::Maybe<driver::package::RegisteredTargetService> TargetRegistrySnapshot::packageTargetService()
    const {
  zc::Vector<driver::package::RegisteredTargetProfile> profiles(profileValues.size());
  for (const auto& profile : profileValues) {
    profiles.add(driver::package::RegisteredTargetProfile::from(profile.nameValue.clone(),
                                                                profile.projectionValue.clone()));
  }
  return driver::package::RegisteredTargetService::fromVerifiedRegistry(
      revisionValue, hostProfileValue.clone(), zc::mv(profiles));
}

TargetSelectionVerificationResult TargetRegistrySnapshot::verify(
    const driver::package::RegisteredTargetSelection& selection) const {
  if (selection.registryRevision() != revisionValue) {
    return TargetSelectionVerificationIssue::RegistryRevisionMismatch;
  }
  for (const auto& profile : profileValues) {
    if (profile.nameValue.text() != selection.profile()) { continue; }
    if (!sameProjection(profile.projectionValue, selection.semanticProjection())) {
      return TargetSelectionVerificationIssue::ProjectionMismatch;
    }
    const auto required = selection.panicStrategy() == driver::package::PackagePanicStrategy::Abort
                              ? BackendPanicStrategy::Abort
                              : BackendPanicStrategy::Unwind;
    for (const auto& specification : profile.specificationValues) {
      if (specification.panicStrategy() == required) {
        return VerifiedTargetSelection(selection.clone(), specification.clone());
      }
    }
    return TargetSelectionVerificationIssue::CapabilityUnavailable;
  }
  return TargetSelectionVerificationIssue::InvalidFact;
}

}  // namespace zomlang::compiler::irgen
