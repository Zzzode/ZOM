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

#include "compiler/identity/key/crate-key.h"

#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

bool isValid(Endianness value) { return value == Endianness::Little || value == Endianness::Big; }

bool isValid(CompilationDomain value) {
  return value == CompilationDomain::Host || value == CompilationDomain::Target;
}

bool isValid(CrateTargetKind value) {
  return value >= CrateTargetKind::Library && value <= CrateTargetKind::BuildScript;
}

bool samePackage(const PackageKey& left, const PackageKey& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return leftBytes.asPtr() == rightBytes.asPtr();
}

bool isUserPackage(const CompilationUnitIdentity& unit) {
  return unit.kind() == CompilationUnitKind::UserPackage;
}

bool isToolchainCore(const CompilationUnitIdentity& unit) {
  return unit.kind() == CompilationUnitKind::Toolchain &&
         unit.toolchain().component() == ToolchainComponent::Core;
}

bool isAcceptedCoreConsumerKind(CompilationDomain domain, CrateTargetKind kind) {
  if (domain == CompilationDomain::Target) {
    return kind == CrateTargetKind::Library || kind == CrateTargetKind::Binary ||
           kind == CrateTargetKind::Test || kind == CrateTargetKind::Benchmark ||
           kind == CrateTargetKind::Example;
  }
  return kind == CrateTargetKind::BuildScript || kind == CrateTargetKind::Library;
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

zc::Maybe<CanonicalTargetSpecificationKey> CanonicalTargetSpecificationKey::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto architecture = TargetComponentName::decodeCanonical(decoder);
  if (architecture == zc::none) { return zc::none; }
  auto vendor = TargetComponentName::decodeCanonical(decoder);
  if (vendor == zc::none) { return zc::none; }
  auto operatingSystem = TargetComponentName::decodeCanonical(decoder);
  if (operatingSystem == zc::none) { return zc::none; }
  auto environment = TargetComponentName::decodeCanonical(decoder);
  if (environment == zc::none) { return zc::none; }
  auto abi = TargetComponentName::decodeCanonical(decoder);
  if (abi == zc::none) { return zc::none; }
  auto pointerWidth = decoder.decodeUint32();
  if (pointerWidth == zc::none) { return zc::none; }
  auto endianness = decoder.decodeUint8();
  if (endianness == zc::none) { return zc::none; }
  ZC_IF_SOME(value, endianness) {
    if (value != static_cast<uint8_t>(Endianness::Little) &&
        value != static_cast<uint8_t>(Endianness::Big)) {
      return zc::none;
    }
  }
  auto features = SortedTargetFeatureSet::decodeCanonical(decoder);
  if (features == zc::none) { return zc::none; }
  ZC_IF_SOME(architectureValue, architecture) {
    ZC_IF_SOME(vendorValue, vendor) {
      ZC_IF_SOME(operatingSystemValue, operatingSystem) {
        ZC_IF_SOME(environmentValue, environment) {
          ZC_IF_SOME(abiValue, abi) {
            ZC_IF_SOME(pointerWidthValue, pointerWidth) {
              ZC_IF_SOME(endiannessValue, endianness) {
                ZC_IF_SOME(featureValues, features) {
                  return from(zc::mv(architectureValue), zc::mv(vendorValue),
                              zc::mv(operatingSystemValue), zc::mv(environmentValue),
                              zc::mv(abiValue), pointerWidthValue,
                              static_cast<Endianness>(endiannessValue), zc::mv(featureValues));
                }
              }
            }
          }
        }
      }
    }
  }
  return zc::none;
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

zc::Maybe<SemanticCompilerOptionsKey> SemanticCompilerOptionsKey::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto editionYear = decoder.decodeUint32();
  if (editionYear == zc::none) { return zc::none; }
  auto useUnicode = decoder.decodeBool();
  if (useUnicode == zc::none) { return zc::none; }
  auto allowDollarIdentifiers = decoder.decodeBool();
  if (allowDollarIdentifiers == zc::none) { return zc::none; }
  auto supportRegexLiterals = decoder.decodeBool();
  if (supportRegexLiterals == zc::none) { return zc::none; }
  ZC_IF_SOME(editionYearValue, editionYear) {
    ZC_IF_SOME(useUnicodeValue, useUnicode) {
      ZC_IF_SOME(allowDollarIdentifiersValue, allowDollarIdentifiers) {
        ZC_IF_SOME(supportRegexLiteralsValue, supportRegexLiterals) {
          return SemanticCompilerOptionsKey(editionYearValue, useUnicodeValue,
                                            allowDollarIdentifiersValue, supportRegexLiteralsValue);
        }
      }
    }
  }
  return zc::none;
}

uint32_t SemanticCompilerOptionsKey::editionYear() const noexcept { return editionYearValue; }

bool SemanticCompilerOptionsKey::useUnicode() const noexcept { return useUnicodeValue; }

bool SemanticCompilerOptionsKey::allowDollarIdentifiers() const noexcept {
  return allowDollarIdentifiersValue;
}

bool SemanticCompilerOptionsKey::supportRegexLiterals() const noexcept {
  return supportRegexLiteralsValue;
}

void SemanticCompilerOptionsKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint32(editionYearValue);
  encoder.encodeBool(useUnicodeValue);
  encoder.encodeBool(allowDollarIdentifiersValue);
  encoder.encodeBool(supportRegexLiteralsValue);
}

BuildScriptProducerKey::BuildScriptProducerKey(const Sha256Digest& digest) noexcept
    : value(digest) {}

BuildScriptProducerKey BuildScriptProducerKey::from(const Sha256Digest& digest) noexcept {
  return BuildScriptProducerKey(digest);
}

zc::Maybe<BuildScriptProducerKey> BuildScriptProducerKey::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto digest = decoder.decodeDigest();
  ZC_IF_SOME(value, digest) { return BuildScriptProducerKey(value); }
  return zc::none;
}

const Sha256Digest& BuildScriptProducerKey::digest() const noexcept { return value; }

void BuildScriptProducerKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeDigest(value);
}

CompilationConfigKey::CompilationConfigKey(
    CompilationDomain domain, CanonicalTargetSpecificationKey&& target,
    SemanticCompilerOptionsKey semanticOptions,
    zc::Maybe<BuildScriptProducerKey>&& buildScriptProducer) noexcept
    : domainValue(domain),
      targetValue(zc::mv(target)),
      semanticOptionsValue(semanticOptions),
      buildScriptProducerValue(zc::mv(buildScriptProducer)) {}

zc::Maybe<CompilationConfigKey> CompilationConfigKey::from(
    CompilationDomain domain, CanonicalTargetSpecificationKey&& target,
    SemanticCompilerOptionsKey semanticOptions,
    zc::Maybe<BuildScriptProducerKey>&& buildScriptProducer) {
  if (!isValid(domain)) { return zc::none; }
  return CompilationConfigKey(domain, zc::mv(target), semanticOptions, zc::mv(buildScriptProducer));
}

zc::Maybe<CompilationConfigKey> CompilationConfigKey::decodeCanonical(CanonicalDecoder& decoder) {
  auto domain = decoder.decodeUint8();
  if (domain == zc::none) { return zc::none; }
  ZC_IF_SOME(value, domain) {
    if (value != static_cast<uint8_t>(CompilationDomain::Host) &&
        value != static_cast<uint8_t>(CompilationDomain::Target)) {
      return zc::none;
    }
  }
  auto target = CanonicalTargetSpecificationKey::decodeCanonical(decoder);
  if (target == zc::none) { return zc::none; }
  auto semanticOptions = SemanticCompilerOptionsKey::decodeCanonical(decoder);
  if (semanticOptions == zc::none) { return zc::none; }
  auto producerPresence = decoder.decodeUint8();
  if (producerPresence == zc::none) { return zc::none; }
  ZC_IF_SOME(domainTag, domain) {
    ZC_IF_SOME(targetValue, target) {
      ZC_IF_SOME(optionsValue, semanticOptions) {
        ZC_IF_SOME(presence, producerPresence) {
          zc::Maybe<BuildScriptProducerKey> producer;
          if (presence == 0x01) {
            auto decoded = BuildScriptProducerKey::decodeCanonical(decoder);
            if (decoded == zc::none) { return zc::none; }
            ZC_IF_SOME(value, decoded) { producer = value; }
          } else if (presence != 0x00) {
            return zc::none;
          }
          return from(static_cast<CompilationDomain>(domainTag), zc::mv(targetValue), optionsValue,
                      zc::mv(producer));
        }
      }
    }
  }
  return zc::none;
}

CompilationConfigKey CompilationConfigKey::clone() const {
  zc::Maybe<BuildScriptProducerKey> producer;
  ZC_IF_SOME(value, buildScriptProducerValue) {
    producer = BuildScriptProducerKey::from(value.digest());
  }
  return CompilationConfigKey(domainValue, targetValue.clone(), semanticOptionsValue,
                              zc::mv(producer));
}

CompilationDomain CompilationConfigKey::domain() const noexcept { return domainValue; }

const CanonicalTargetSpecificationKey& CompilationConfigKey::target() const noexcept {
  return targetValue;
}

const SemanticCompilerOptionsKey& CompilationConfigKey::semanticOptions() const noexcept {
  return semanticOptionsValue;
}

bool CompilationConfigKey::hasBuildScriptProducer() const noexcept {
  return buildScriptProducerValue != zc::none;
}

void CompilationConfigKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(domainValue));
  targetValue.encode(encoder);
  semanticOptionsValue.encode(encoder);
  ZC_IF_SOME(producer, buildScriptProducerValue) {
    encoder.encodeSome();
    producer.encode(encoder);
  } else {
    encoder.encodeNone();
  }
}

CrateKey::CrateKey(CompilationUnitIdentity&& unit, CrateTargetKind kind, TargetName&& targetName,
                   CompilationConfigKey&& compilation) noexcept
    : unitValue(zc::mv(unit)),
      kindValue(kind),
      targetNameValue(zc::mv(targetName)),
      compilationValue(zc::mv(compilation)) {}

zc::Maybe<CrateKey> CrateKey::from(CompilationUnitIdentity&& unit, CrateTargetKind kind,
                                   TargetName&& targetName, CompilationConfigKey&& compilation) {
  if (!isValid(kind)) { return zc::none; }
  if (isToolchainCore(unit) &&
      (kind != CrateTargetKind::Library || targetName.text() != "core"_zc ||
       compilation.hasBuildScriptProducer() ||
       compilation.semanticOptions().editionYear() != 2026)) {
    return zc::none;
  }
  return CrateKey(zc::mv(unit), kind, zc::mv(targetName), zc::mv(compilation));
}

zc::Maybe<CrateKey> CrateKey::decodeCanonical(CanonicalDecoder& decoder) {
  auto unit = CompilationUnitIdentity::decodeCanonical(decoder);
  if (unit == zc::none) { return zc::none; }
  auto kind = decoder.decodeUint8();
  if (kind == zc::none) { return zc::none; }
  ZC_IF_SOME(value, kind) {
    if (value < static_cast<uint8_t>(CrateTargetKind::Library) ||
        value > static_cast<uint8_t>(CrateTargetKind::BuildScript)) {
      return zc::none;
    }
  }
  auto targetName = TargetName::decodeCanonical(decoder);
  if (targetName == zc::none) { return zc::none; }
  auto compilation = CompilationConfigKey::decodeCanonical(decoder);
  if (compilation == zc::none) { return zc::none; }
  ZC_IF_SOME(unitValue, unit) {
    ZC_IF_SOME(kindValue, kind) {
      ZC_IF_SOME(targetNameValue, targetName) {
        ZC_IF_SOME(compilationValue, compilation) {
          return from(zc::mv(unitValue), static_cast<CrateTargetKind>(kindValue),
                      zc::mv(targetNameValue), zc::mv(compilationValue));
        }
      }
    }
  }
  return zc::none;
}

CrateKey CrateKey::clone() const {
  return CrateKey(unitValue.clone(), kindValue, targetNameValue.clone(), compilationValue.clone());
}

const CompilationUnitIdentity& CrateKey::unit() const noexcept { return unitValue; }

CrateTargetKind CrateKey::targetKind() const noexcept { return kindValue; }

zc::StringPtr CrateKey::targetName() const noexcept { return targetNameValue.text(); }

const CompilationConfigKey& CrateKey::compilation() const noexcept { return compilationValue; }

const SemanticCompilerOptionsKey& CrateKey::semanticOptions() const noexcept {
  return compilationValue.semanticOptions();
}

void CrateKey::encode(CanonicalEncoder& encoder) const {
  unitValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  targetNameValue.encode(encoder);
  compilationValue.encode(encoder);
}

zc::Array<uint8_t> CrateKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

zc::Maybe<CrateKey> projectToolchainCoreCrate(const CrateKey& consumer) {
  if (!isUserPackage(consumer.unit()) ||
      !isAcceptedCoreConsumerKind(consumer.compilation().domain(), consumer.targetKind())) {
    return zc::none;
  }
  const auto& consumerOptions = consumer.semanticOptions();
  auto compilation = CompilationConfigKey::from(
      consumer.compilation().domain(), consumer.compilation().target().clone(),
      SemanticCompilerOptionsKey::from(2026, consumerOptions.useUnicode(),
                                       consumerOptions.allowDollarIdentifiers(),
                                       consumerOptions.supportRegexLiterals()),
      zc::Maybe<BuildScriptProducerKey>());
  auto target = TargetName::fromCanonical("core"_zc);
  if (compilation == zc::none || target == zc::none) { return zc::none; }
  ZC_IF_SOME(targetValue, target) {
    ZC_IF_SOME(compilationValue, compilation) {
      return CrateKey::from(CompilationUnitIdentity::toolchain(ToolchainUnitKey::core()),
                            CrateTargetKind::Library, zc::mv(targetValue),
                            zc::mv(compilationValue));
    }
  }
  return zc::none;
}

CrateDependencyOrigin::CrateDependencyOrigin(UserPackageCrateDependencyOrigin&& origin) noexcept
    : value(zc::mv(origin)) {}

CrateDependencyOrigin::CrateDependencyOrigin(ToolchainCoreCrateDependencyOrigin&& origin) noexcept
    : value(zc::mv(origin)) {}

CrateDependencyOrigin CrateDependencyOrigin::userPackage(PackageDependencyEdgeKey&& edge) {
  return CrateDependencyOrigin(UserPackageCrateDependencyOrigin{zc::mv(edge)});
}

CrateDependencyOrigin CrateDependencyOrigin::toolchainCore() {
  return CrateDependencyOrigin(ToolchainCoreCrateDependencyOrigin{});
}

zc::Maybe<CrateDependencyOrigin> CrateDependencyOrigin::decodeCanonical(CanonicalDecoder& decoder) {
  auto kind = decoder.decodeUint8();
  ZC_IF_SOME(tag, kind) {
    switch (static_cast<CrateDependencyOriginKind>(tag)) {
      case CrateDependencyOriginKind::UserPackage: {
        auto edge = PackageDependencyEdgeKey::decodeCanonical(decoder);
        ZC_IF_SOME(value, edge) { return userPackage(zc::mv(value)); }
        return zc::none;
      }
      case CrateDependencyOriginKind::ToolchainCore:
        return toolchainCore();
    }
  }
  return zc::none;
}

CrateDependencyOrigin CrateDependencyOrigin::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(origin, UserPackageCrateDependencyOrigin) {
      return userPackage(origin.edge.clone());
    }
    ZC_CASE_ONEOF(origin, ToolchainCoreCrateDependencyOrigin) { return toolchainCore(); }
  }
  ZC_UNREACHABLE
}

CrateDependencyOriginKind CrateDependencyOrigin::kind() const noexcept {
  if (value.is<UserPackageCrateDependencyOrigin>()) {
    return CrateDependencyOriginKind::UserPackage;
  }
  return CrateDependencyOriginKind::ToolchainCore;
}

const PackageDependencyEdgeKey& CrateDependencyOrigin::userPackageEdge() const {
  return value.get<UserPackageCrateDependencyOrigin>().edge;
}

void CrateDependencyOrigin::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(origin, UserPackageCrateDependencyOrigin) { origin.edge.encode(encoder); }
    ZC_CASE_ONEOF(origin, ToolchainCoreCrateDependencyOrigin) {}
  }
}

CrateDependencyEdgeKey::CrateDependencyEdgeKey(CrateDependencyOrigin&& origin, CrateKey&& consumer,
                                               CrateKey&& provider) noexcept
    : originValue(zc::mv(origin)),
      consumerValue(zc::mv(consumer)),
      providerValue(zc::mv(provider)) {}

zc::Maybe<CrateDependencyEdgeKey> CrateDependencyEdgeKey::from(CrateDependencyOrigin&& origin,
                                                               CrateKey&& consumer,
                                                               CrateKey&& provider) {
  if (origin.kind() == CrateDependencyOriginKind::UserPackage) {
    const auto& packageEdge = origin.userPackageEdge();
    if (!isUserPackage(consumer.unit()) || !isUserPackage(provider.unit()) ||
        !samePackage(packageEdge.consumer(), consumer.unit().userPackage()) ||
        !samePackage(packageEdge.provider(), provider.unit().userPackage())) {
      return zc::none;
    }
  } else {
    auto expectedProvider = projectToolchainCoreCrate(consumer);
    bool providerMatchesProjection = false;
    ZC_IF_SOME(expected, expectedProvider) {
      providerMatchesProjection = expected.encode().asPtr() == provider.encode().asPtr();
    }
    if (!isUserPackage(consumer.unit()) || !isToolchainCore(provider.unit()) ||
        provider.targetKind() != CrateTargetKind::Library || provider.targetName() != "core"_zc ||
        !isAcceptedCoreConsumerKind(consumer.compilation().domain(), consumer.targetKind()) ||
        !providerMatchesProjection) {
      return zc::none;
    }
  }
  return CrateDependencyEdgeKey(zc::mv(origin), zc::mv(consumer), zc::mv(provider));
}

zc::Maybe<CrateDependencyEdgeKey> CrateDependencyEdgeKey::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto origin = CrateDependencyOrigin::decodeCanonical(decoder);
  if (origin == zc::none) { return zc::none; }
  auto consumer = CrateKey::decodeCanonical(decoder);
  if (consumer == zc::none) { return zc::none; }
  auto provider = CrateKey::decodeCanonical(decoder);
  if (provider == zc::none) { return zc::none; }
  ZC_IF_SOME(originValue, origin) {
    ZC_IF_SOME(consumerValue, consumer) {
      ZC_IF_SOME(providerValue, provider) {
        return from(zc::mv(originValue), zc::mv(consumerValue), zc::mv(providerValue));
      }
    }
  }
  return zc::none;
}

CrateDependencyEdgeKey CrateDependencyEdgeKey::clone() const {
  return CrateDependencyEdgeKey(originValue.clone(), consumerValue.clone(), providerValue.clone());
}

const CrateDependencyOrigin& CrateDependencyEdgeKey::origin() const noexcept { return originValue; }

const CrateKey& CrateDependencyEdgeKey::consumer() const noexcept { return consumerValue; }

const CrateKey& CrateDependencyEdgeKey::provider() const noexcept { return providerValue; }

void CrateDependencyEdgeKey::encode(CanonicalEncoder& encoder) const {
  originValue.encode(encoder);
  consumerValue.encode(encoder);
  providerValue.encode(encoder);
}

zc::Array<uint8_t> CrateDependencyEdgeKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
