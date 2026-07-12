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

#include "zomlang/compiler/identity/build-script-key.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const auto common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

template <typename Value, typename Encode>
bool sortUnique(zc::Vector<Value>& values, Encode encode) {
  for (size_t index = 1; index < values.size(); ++index) {
    size_t position = index;
    while (position > 0) {
      const auto leftBytes = encode(values[position - 1]);
      const auto rightBytes = encode(values[position]);
      if (compareBytes(leftBytes, rightBytes) <= 0) { break; }
      Value temporary = zc::mv(values[position - 1]);
      values[position - 1] = zc::mv(values[position]);
      values[position] = zc::mv(temporary);
      --position;
    }
  }
  for (size_t index = 1; index < values.size(); ++index) {
    const auto previous = encode(values[index - 1]);
    const auto current = encode(values[index]);
    if (compareBytes(previous, current) == 0) { return false; }
  }
  return true;
}

zc::Array<uint8_t> encodeDigestEntry(const BuildScriptDigestEntry& value) {
  CanonicalEncoder encoder;
  value.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> encodePath(const CanonicalRelativePath& value) {
  CanonicalEncoder encoder;
  value.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> encodeEnvironmentEntry(const BuildScriptEnvironmentEntry& value) {
  CanonicalEncoder encoder;
  value.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> cloneBytes(zc::ArrayPtr<const uint8_t> source) {
  zc::Vector<uint8_t> result(source.size());
  result.addAll(source);
  return result.releaseAsArray();
}

bool uniqueDigestKeys(zc::ArrayPtr<const BuildScriptDigestEntry> values) {
  for (size_t index = 1; index < values.size(); ++index) {
    if (compareBytes(encodePath(values[index - 1].path()), encodePath(values[index].path())) == 0) {
      return false;
    }
  }
  return true;
}

bool uniqueEnvironmentKeys(zc::ArrayPtr<const BuildScriptEnvironmentEntry> values) {
  for (size_t index = 1; index < values.size(); ++index) {
    if (values[index - 1].name() == values[index].name()) { return false; }
  }
  return true;
}

}  // namespace

BuildScriptDigestEntry::BuildScriptDigestEntry(CanonicalRelativePath&& path,
                                               const Sha256Digest& digest) noexcept
    : pathValue(zc::mv(path)), digestValue(digest) {}

BuildScriptDigestEntry BuildScriptDigestEntry::from(CanonicalRelativePath&& path,
                                                    const Sha256Digest& digest) {
  return BuildScriptDigestEntry(zc::mv(path), digest);
}

BuildScriptDigestEntry BuildScriptDigestEntry::clone() const {
  return BuildScriptDigestEntry(pathValue.clone(), digestValue);
}

const CanonicalRelativePath& BuildScriptDigestEntry::path() const noexcept { return pathValue; }

const Sha256Digest& BuildScriptDigestEntry::digest() const noexcept { return digestValue; }

void BuildScriptDigestEntry::encode(CanonicalEncoder& encoder) const {
  pathValue.encode(encoder);
  encoder.encodeDigest(digestValue);
}

BuildScriptEnvironmentEntry::BuildScriptEnvironmentEntry(SemanticEnvironmentName&& name,
                                                         zc::Array<uint8_t>&& value) noexcept
    : nameValue(zc::mv(name)), byteValue(zc::mv(value)) {}

BuildScriptEnvironmentEntry BuildScriptEnvironmentEntry::from(SemanticEnvironmentName&& name,
                                                              zc::Array<uint8_t>&& value) {
  return BuildScriptEnvironmentEntry(zc::mv(name), zc::mv(value));
}

BuildScriptEnvironmentEntry BuildScriptEnvironmentEntry::clone() const {
  return BuildScriptEnvironmentEntry(nameValue.clone(), cloneBytes(byteValue));
}

zc::StringPtr BuildScriptEnvironmentEntry::name() const noexcept { return nameValue.text(); }

zc::ArrayPtr<const uint8_t> BuildScriptEnvironmentEntry::value() const noexcept {
  return byteValue;
}

void BuildScriptEnvironmentEntry::encode(CanonicalEncoder& encoder) const {
  nameValue.encode(encoder);
  encoder.encodeByteString(byteValue);
}

PreparatoryBuildScriptKey::PreparatoryBuildScriptKey(
    PackageKey&& package, TargetName&& targetName, CanonicalTargetSpecificationKey&& hostTarget,
    SemanticCompilerOptionsKey semanticOptions, zc::Vector<PackageKey>&& buildDependencies) noexcept
    : packageValue(zc::mv(package)),
      targetNameValue(zc::mv(targetName)),
      hostTargetValue(zc::mv(hostTarget)),
      semanticOptionsValue(semanticOptions),
      buildDependencyValues(zc::mv(buildDependencies)) {}

zc::Maybe<PreparatoryBuildScriptKey> PreparatoryBuildScriptKey::from(
    PackageKey&& package, TargetName&& targetName, CanonicalTargetSpecificationKey&& hostTarget,
    SemanticCompilerOptionsKey semanticOptions, zc::Vector<PackageKey>&& buildDependencies) {
  if (!sortUnique(buildDependencies, [](const PackageKey& value) { return value.encode(); })) {
    return zc::none;
  }
  return PreparatoryBuildScriptKey(zc::mv(package), zc::mv(targetName), zc::mv(hostTarget),
                                   semanticOptions, zc::mv(buildDependencies));
}

PreparatoryBuildScriptKey PreparatoryBuildScriptKey::clone() const {
  zc::Vector<PackageKey> dependencies(buildDependencyValues.size());
  for (const auto& dependency : buildDependencyValues) { dependencies.add(dependency.clone()); }
  return PreparatoryBuildScriptKey(packageValue.clone(), targetNameValue.clone(),
                                   hostTargetValue.clone(), semanticOptionsValue,
                                   zc::mv(dependencies));
}

const PackageKey& PreparatoryBuildScriptKey::package() const noexcept { return packageValue; }

zc::StringPtr PreparatoryBuildScriptKey::targetName() const noexcept {
  return targetNameValue.text();
}

zc::ArrayPtr<const PackageKey> PreparatoryBuildScriptKey::buildDependencies() const noexcept {
  return buildDependencyValues;
}

void PreparatoryBuildScriptKey::encode(CanonicalEncoder& encoder) const {
  packageValue.encode(encoder);
  targetNameValue.encode(encoder);
  hostTargetValue.encode(encoder);
  semanticOptionsValue.encode(encoder);
  encoder.encodeSequenceSize(buildDependencyValues.size());
  for (const auto& dependency : buildDependencyValues) { dependency.encode(encoder); }
}

zc::Array<uint8_t> PreparatoryBuildScriptKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

BuildScriptOutputRecord::BuildScriptOutputRecord(
    PreparatoryBuildScriptKey&& preparatory, zc::Vector<BuildScriptDigestEntry>&& sourceDigests,
    zc::Vector<BuildScriptEnvironmentEntry>&& declaredEnvironment,
    zc::Vector<BuildScriptDigestEntry>&& generatedSources,
    zc::Vector<BuildScriptEnvironmentEntry>&& exportedSemanticEnvironment) noexcept
    : preparatoryValue(zc::mv(preparatory)),
      sourceDigestValues(zc::mv(sourceDigests)),
      declaredEnvironmentValues(zc::mv(declaredEnvironment)),
      generatedSourceValues(zc::mv(generatedSources)),
      exportedEnvironmentValues(zc::mv(exportedSemanticEnvironment)) {}

zc::Maybe<BuildScriptOutputRecord> BuildScriptOutputRecord::from(
    PreparatoryBuildScriptKey&& preparatory, zc::Vector<BuildScriptDigestEntry>&& sourceDigests,
    zc::Vector<BuildScriptEnvironmentEntry>&& declaredEnvironment,
    zc::Vector<BuildScriptDigestEntry>&& generatedSources,
    zc::Vector<BuildScriptEnvironmentEntry>&& exportedSemanticEnvironment) {
  if (!sortUnique(sourceDigests, encodeDigestEntry) || !uniqueDigestKeys(sourceDigests) ||
      !sortUnique(declaredEnvironment, encodeEnvironmentEntry) ||
      !uniqueEnvironmentKeys(declaredEnvironment) ||
      !sortUnique(generatedSources, encodeDigestEntry) || !uniqueDigestKeys(generatedSources) ||
      !sortUnique(exportedSemanticEnvironment, encodeEnvironmentEntry) ||
      !uniqueEnvironmentKeys(exportedSemanticEnvironment)) {
    return zc::none;
  }
  return BuildScriptOutputRecord(zc::mv(preparatory), zc::mv(sourceDigests),
                                 zc::mv(declaredEnvironment), zc::mv(generatedSources),
                                 zc::mv(exportedSemanticEnvironment));
}

BuildScriptOutputRecord BuildScriptOutputRecord::clone() const {
  zc::Vector<BuildScriptDigestEntry> sources(sourceDigestValues.size());
  for (const auto& value : sourceDigestValues) { sources.add(value.clone()); }
  zc::Vector<BuildScriptEnvironmentEntry> environment(declaredEnvironmentValues.size());
  for (const auto& value : declaredEnvironmentValues) { environment.add(value.clone()); }
  zc::Vector<BuildScriptDigestEntry> generated(generatedSourceValues.size());
  for (const auto& value : generatedSourceValues) { generated.add(value.clone()); }
  zc::Vector<BuildScriptEnvironmentEntry> exported(exportedEnvironmentValues.size());
  for (const auto& value : exportedEnvironmentValues) { exported.add(value.clone()); }
  return BuildScriptOutputRecord(preparatoryValue.clone(), zc::mv(sources), zc::mv(environment),
                                 zc::mv(generated), zc::mv(exported));
}

const PreparatoryBuildScriptKey& BuildScriptOutputRecord::preparatoryKey() const noexcept {
  return preparatoryValue;
}

zc::ArrayPtr<const BuildScriptDigestEntry> BuildScriptOutputRecord::sourceDigests() const noexcept {
  return sourceDigestValues;
}

zc::ArrayPtr<const BuildScriptEnvironmentEntry> BuildScriptOutputRecord::declaredEnvironment()
    const noexcept {
  return declaredEnvironmentValues;
}

zc::ArrayPtr<const BuildScriptDigestEntry> BuildScriptOutputRecord::generatedSources()
    const noexcept {
  return generatedSourceValues;
}

zc::ArrayPtr<const BuildScriptEnvironmentEntry> BuildScriptOutputRecord::exportedEnvironment()
    const noexcept {
  return exportedEnvironmentValues;
}

void BuildScriptOutputRecord::encode(CanonicalEncoder& encoder) const {
  preparatoryValue.encode(encoder);
  encoder.encodeSequenceSize(sourceDigestValues.size());
  for (const auto& value : sourceDigestValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(declaredEnvironmentValues.size());
  for (const auto& value : declaredEnvironmentValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(generatedSourceValues.size());
  for (const auto& value : generatedSourceValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(exportedEnvironmentValues.size());
  for (const auto& value : exportedEnvironmentValues) { value.encode(encoder); }
}

zc::Array<uint8_t> BuildScriptOutputRecord::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

BuildScriptOutputKey BuildScriptOutputRecord::outputKey() const {
  constexpr auto domain = "zom.build-script-output.v0"_zc;
  auto record = encode();
  zc::Vector<uint8_t> preimage(domain.size() + 1 + record.size());
  preimage.addAll(domain.asBytes());
  preimage.add(0);
  preimage.addAll(record);
  auto digest = sha256(preimage);
  ZC_IF_SOME(value, digest) { return BuildScriptOutputKey::from(value); }
  ZC_UNREACHABLE;
}

}  // namespace zomlang::compiler::identity
