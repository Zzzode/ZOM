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

#include "zomlang/compiler/identity/crate-key.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

bool isValid(Endianness value) { return value == Endianness::Little || value == Endianness::Big; }

bool isValid(CompilationDomain value) {
  return value == CompilationDomain::Host || value == CompilationDomain::Target;
}

bool isValid(CrateTargetKind value) {
  return value >= CrateTargetKind::Library && value <= CrateTargetKind::BuildScript;
}

}  // namespace

CanonicalTargetSpecificationKey::CanonicalTargetSpecificationKey(
    TargetComponentName&& architecture, TargetComponentName&& vendor,
    TargetComponentName&& operatingSystem, TargetComponentName&& environment,
    TargetComponentName&& abi, uint32_t pointerWidth, Endianness endianness,
    SortedTargetFeatureSet&& semanticFeatures) noexcept
    : architectureValue(zc::mv(architecture)),
      vendorValue(zc::mv(vendor)),
      operatingSystemValue(zc::mv(operatingSystem)),
      environmentValue(zc::mv(environment)),
      abiValue(zc::mv(abi)),
      pointerWidthValue(pointerWidth),
      endiannessValue(endianness),
      featureValue(zc::mv(semanticFeatures)) {}

zc::Maybe<CanonicalTargetSpecificationKey> CanonicalTargetSpecificationKey::from(
    TargetComponentName&& architecture, TargetComponentName&& vendor,
    TargetComponentName&& operatingSystem, TargetComponentName&& environment,
    TargetComponentName&& abi, uint32_t pointerWidth, Endianness endianness,
    SortedTargetFeatureSet&& semanticFeatures) {
  if (pointerWidth == 0 || pointerWidth % 8 != 0 || !isValid(endianness)) { return zc::none; }
  return CanonicalTargetSpecificationKey(zc::mv(architecture), zc::mv(vendor),
                                         zc::mv(operatingSystem), zc::mv(environment), zc::mv(abi),
                                         pointerWidth, endianness, zc::mv(semanticFeatures));
}

CanonicalTargetSpecificationKey CanonicalTargetSpecificationKey::clone() const {
  return CanonicalTargetSpecificationKey(architectureValue.clone(), vendorValue.clone(),
                                         operatingSystemValue.clone(), environmentValue.clone(),
                                         abiValue.clone(), pointerWidthValue, endiannessValue,
                                         featureValue.clone());
}

zc::StringPtr CanonicalTargetSpecificationKey::architecture() const noexcept {
  return architectureValue.text();
}

uint32_t CanonicalTargetSpecificationKey::pointerWidth() const noexcept {
  return pointerWidthValue;
}

Endianness CanonicalTargetSpecificationKey::endianness() const noexcept { return endiannessValue; }

void CanonicalTargetSpecificationKey::encode(CanonicalEncoder& encoder) const {
  architectureValue.encode(encoder);
  vendorValue.encode(encoder);
  operatingSystemValue.encode(encoder);
  environmentValue.encode(encoder);
  abiValue.encode(encoder);
  encoder.encodeUint32(pointerWidthValue);
  encoder.encodeUint8(static_cast<uint8_t>(endiannessValue));
  featureValue.encode(encoder);
}

SemanticCompilerOptionsKey::SemanticCompilerOptionsKey(uint32_t editionYear, bool useUnicode,
                                                       bool allowDollarIdentifiers,
                                                       bool supportRegexLiterals) noexcept
    : editionYearValue(editionYear),
      useUnicodeValue(useUnicode),
      allowDollarIdentifiersValue(allowDollarIdentifiers),
      supportRegexLiteralsValue(supportRegexLiterals) {}

SemanticCompilerOptionsKey SemanticCompilerOptionsKey::from(uint32_t editionYear, bool useUnicode,
                                                            bool allowDollarIdentifiers,
                                                            bool supportRegexLiterals) noexcept {
  return SemanticCompilerOptionsKey(editionYear, useUnicode, allowDollarIdentifiers,
                                    supportRegexLiterals);
}

void SemanticCompilerOptionsKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint32(editionYearValue);
  encoder.encodeBool(useUnicodeValue);
  encoder.encodeBool(allowDollarIdentifiersValue);
  encoder.encodeBool(supportRegexLiteralsValue);
}

BuildScriptOutputKey::BuildScriptOutputKey(const Sha256Digest& digest) noexcept : value(digest) {}

BuildScriptOutputKey BuildScriptOutputKey::from(const Sha256Digest& digest) noexcept {
  return BuildScriptOutputKey(digest);
}

const Sha256Digest& BuildScriptOutputKey::digest() const noexcept { return value; }

void BuildScriptOutputKey::encode(CanonicalEncoder& encoder) const { encoder.encodeDigest(value); }

CompilationConfigKey::CompilationConfigKey(
    CompilationDomain domain, CanonicalTargetSpecificationKey&& target,
    SemanticCompilerOptionsKey semanticOptions,
    zc::Maybe<BuildScriptOutputKey>&& buildScriptOutput) noexcept
    : domainValue(domain),
      targetValue(zc::mv(target)),
      semanticOptionsValue(semanticOptions),
      buildScriptOutputValue(zc::mv(buildScriptOutput)) {}

zc::Maybe<CompilationConfigKey> CompilationConfigKey::from(
    CompilationDomain domain, CanonicalTargetSpecificationKey&& target,
    SemanticCompilerOptionsKey semanticOptions,
    zc::Maybe<BuildScriptOutputKey>&& buildScriptOutput) {
  if (!isValid(domain)) { return zc::none; }
  return CompilationConfigKey(domain, zc::mv(target), semanticOptions, zc::mv(buildScriptOutput));
}

CompilationConfigKey CompilationConfigKey::clone() const {
  zc::Maybe<BuildScriptOutputKey> output;
  ZC_IF_SOME(value, buildScriptOutputValue) { output = BuildScriptOutputKey::from(value.digest()); }
  return CompilationConfigKey(domainValue, targetValue.clone(), semanticOptionsValue,
                              zc::mv(output));
}

void CompilationConfigKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(domainValue));
  targetValue.encode(encoder);
  semanticOptionsValue.encode(encoder);
  ZC_IF_SOME(output, buildScriptOutputValue) {
    encoder.encodeSome();
    output.encode(encoder);
  }
  else { encoder.encodeNone(); }
}

CrateKey::CrateKey(PackageKey&& package, CrateTargetKind kind, TargetName&& targetName,
                   CompilationConfigKey&& compilation) noexcept
    : packageValue(zc::mv(package)),
      kindValue(kind),
      targetNameValue(zc::mv(targetName)),
      compilationValue(zc::mv(compilation)) {}

zc::Maybe<CrateKey> CrateKey::from(PackageKey&& package, CrateTargetKind kind,
                                   TargetName&& targetName, CompilationConfigKey&& compilation) {
  if (!isValid(kind)) { return zc::none; }
  return CrateKey(zc::mv(package), kind, zc::mv(targetName), zc::mv(compilation));
}

CrateKey CrateKey::clone() const {
  return CrateKey(packageValue.clone(), kindValue, targetNameValue.clone(),
                  compilationValue.clone());
}

const PackageKey& CrateKey::package() const noexcept { return packageValue; }

CrateTargetKind CrateKey::targetKind() const noexcept { return kindValue; }

zc::StringPtr CrateKey::targetName() const noexcept { return targetNameValue.text(); }

void CrateKey::encode(CanonicalEncoder& encoder) const {
  packageValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  targetNameValue.encode(encoder);
  compilationValue.encode(encoder);
}

zc::Array<uint8_t> CrateKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

CrateDependencyEdgeKey::CrateDependencyEdgeKey(PackageDependencyEdgeKey&& packageEdge,
                                               CrateKey&& consumer, CrateKey&& provider) noexcept
    : packageEdgeValue(zc::mv(packageEdge)),
      consumerValue(zc::mv(consumer)),
      providerValue(zc::mv(provider)) {}

CrateDependencyEdgeKey CrateDependencyEdgeKey::from(PackageDependencyEdgeKey&& packageEdge,
                                                    CrateKey&& consumer, CrateKey&& provider) {
  return CrateDependencyEdgeKey(zc::mv(packageEdge), zc::mv(consumer), zc::mv(provider));
}

CrateDependencyEdgeKey CrateDependencyEdgeKey::clone() const {
  return CrateDependencyEdgeKey(packageEdgeValue.clone(), consumerValue.clone(),
                                providerValue.clone());
}

const PackageDependencyEdgeKey& CrateDependencyEdgeKey::packageEdge() const noexcept {
  return packageEdgeValue;
}

const CrateKey& CrateDependencyEdgeKey::consumer() const noexcept { return consumerValue; }

const CrateKey& CrateDependencyEdgeKey::provider() const noexcept { return providerValue; }

void CrateDependencyEdgeKey::encode(CanonicalEncoder& encoder) const {
  packageEdgeValue.encode(encoder);
  consumerValue.encode(encoder);
  providerValue.encode(encoder);
}

zc::Array<uint8_t> CrateDependencyEdgeKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
