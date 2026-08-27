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

#include "zomlang/compiler/identity/key/compilation-unit-key.h"

#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

constexpr auto kToolchainUnitKeyDomain = "zom.toolchain-core-key"_zc;

bool hasDomain(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr domain) {
  if (bytes.size() <= domain.size() || bytes[domain.size()] != 0x00) { return false; }
  for (size_t index = 0; index < domain.size(); ++index) {
    if (bytes[index] != static_cast<uint8_t>(domain[index])) { return false; }
  }
  return true;
}

}  // namespace

ToolchainUnitKey::ToolchainUnitKey(ToolchainComponent component) noexcept
    : componentValue(component) {}

ToolchainUnitKey ToolchainUnitKey::core() noexcept {
  return ToolchainUnitKey(ToolchainComponent::Core);
}

zc::Maybe<ToolchainUnitKey> ToolchainUnitKey::decode(zc::ArrayPtr<const uint8_t> bytes) {
  if (!hasDomain(bytes, kToolchainUnitKeyDomain)) { return zc::none; }
  CanonicalDecoder decoder(bytes.slice(kToolchainUnitKeyDomain.size() + 1, bytes.size()));
  auto key = decodeCanonical(decoder);
  if (key == zc::none || !decoder.finished()) { return zc::none; }
  return key;
}

zc::Maybe<ToolchainUnitKey> ToolchainUnitKey::decodeCanonical(CanonicalDecoder& decoder) {
  auto component = decoder.decodeUint8();
  ZC_IF_SOME(tag, component) {
    if (tag == static_cast<uint8_t>(ToolchainComponent::Core)) { return core(); }
  }
  return zc::none;
}

ToolchainComponent ToolchainUnitKey::component() const noexcept { return componentValue; }

void ToolchainUnitKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(componentValue));
}

zc::Array<uint8_t> ToolchainUnitKey::encode() const {
  CanonicalEncoder recordEncoder;
  encode(recordEncoder);
  const auto record = recordEncoder.finish();
  zc::Vector<uint8_t> bytes(kToolchainUnitKeyDomain.size() + 1 + record.size());
  bytes.addAll(kToolchainUnitKeyDomain.asBytes());
  bytes.add(0x00);
  bytes.addAll(record);
  return bytes.releaseAsArray();
}

CompilationUnitIdentity::CompilationUnitIdentity(UserPackageCompilationUnit&& unit) noexcept
    : value(zc::mv(unit)) {}

CompilationUnitIdentity::CompilationUnitIdentity(ToolchainCompilationUnit&& unit) noexcept
    : value(zc::mv(unit)) {}

CompilationUnitIdentity CompilationUnitIdentity::userPackage(PackageKey&& package) {
  return CompilationUnitIdentity(UserPackageCompilationUnit{zc::mv(package)});
}

CompilationUnitIdentity CompilationUnitIdentity::toolchain(ToolchainUnitKey toolchain) {
  return CompilationUnitIdentity(ToolchainCompilationUnit{toolchain});
}

zc::Maybe<CompilationUnitIdentity> CompilationUnitIdentity::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto kind = decoder.decodeUint8();
  ZC_IF_SOME(tag, kind) {
    switch (static_cast<CompilationUnitKind>(tag)) {
      case CompilationUnitKind::UserPackage: {
        auto package = PackageKey::decodeCanonical(decoder);
        ZC_IF_SOME(value, package) { return userPackage(zc::mv(value)); }
        return zc::none;
      }
      case CompilationUnitKind::Toolchain: {
        auto toolchain = ToolchainUnitKey::decodeCanonical(decoder);
        ZC_IF_SOME(value, toolchain) { return CompilationUnitIdentity::toolchain(value); }
        return zc::none;
      }
    }
  }
  return zc::none;
}

CompilationUnitIdentity CompilationUnitIdentity::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(unit, UserPackageCompilationUnit) { return userPackage(unit.package.clone()); }
    ZC_CASE_ONEOF(unit, ToolchainCompilationUnit) { return toolchain(ToolchainUnitKey::core()); }
  }
  ZC_UNREACHABLE
}

CompilationUnitKind CompilationUnitIdentity::kind() const noexcept {
  if (value.is<UserPackageCompilationUnit>()) { return CompilationUnitKind::UserPackage; }
  return CompilationUnitKind::Toolchain;
}

const PackageKey& CompilationUnitIdentity::userPackage() const {
  return value.get<UserPackageCompilationUnit>().package;
}

const ToolchainUnitKey& CompilationUnitIdentity::toolchain() const {
  return value.get<ToolchainCompilationUnit>().toolchain;
}

void CompilationUnitIdentity::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(unit, UserPackageCompilationUnit) { unit.package.encode(encoder); }
    ZC_CASE_ONEOF(unit, ToolchainCompilationUnit) { unit.toolchain.encode(encoder); }
  }
}

zc::Array<uint8_t> CompilationUnitIdentity::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
