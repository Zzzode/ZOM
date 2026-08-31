// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/query/binding/incremental-binding-query-adapter.h"

#include "compiler/binder/graph/module-skeleton-query.h"
#include "compiler/driver/core/query.h"
#include "compiler/driver/query/binding/active-definition-authority-query.h"
#include "compiler/driver/query/binding/active-identity-membership-query.h"
#include "compiler/driver/query/binding/incremental-package-graph-query-input.h"
#include "compiler/driver/query/binding/named-identity-inventory-query.h"
#include "compiler/driver/query/binding/named-item-query.h"
#include "compiler/driver/query/binding/owner-body-query.h"
#include "compiler/driver/query/module-graph/incremental-module-resolution-query.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/parser/query/parse-source-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr uint64_t kMaximumModuleKeyBytes = 16 * 1024;
constexpr uint64_t kMaximumSourceKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumPackageOrCrateKeyBytes = 2 * 1024 * 1024;
constexpr uint64_t kMaximumPackageRoots = 4096;
constexpr uint64_t kMaximumCompilationRoots = 8192;
constexpr uint64_t kMaximumActiveCrates = 4096;
constexpr uint64_t kMaximumEncodedPackageOrCrateSetBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumActiveSources = 65536;
constexpr uint64_t kMaximumEncodedModuleSetBytes = 64 * 1024 * 1024;
constexpr zc::StringPtr kCompilationRootSetDomain = "zom.query.compilation-root-set"_zc;

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  if (left.size() < right.size()) return -1;
  if (left.size() > right.size()) return 1;
  return 0;
}

}  // namespace

StablePackageQueryKey::StablePackageQueryKey(zc::Array<uint8_t>&& canonicalPackageBytes) noexcept
    : canonicalPackageBytesField(zc::mv(canonicalPackageBytes)) {}

zc::Maybe<StablePackageQueryKey> StablePackageQueryKey::fromVerified(
    const identity::PackageKey& package) {
  return decodeBounded(package.encode().asPtr());
}

zc::Maybe<StablePackageQueryKey> StablePackageQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumPackageOrCrateKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto package = identity::PackageKey::decodeCanonical(decoder);
  if (package == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, package) {
    auto canonical = value.encode();
    if (canonical.asPtr() != bytes) { return zc::none; }
  }
  return StablePackageQueryKey(zc::heapArray<uint8_t>(bytes));
}

StablePackageQueryKey StablePackageQueryKey::clone() const {
  return StablePackageQueryKey(zc::heapArray<uint8_t>(canonicalPackageBytesField.asPtr()));
}

zc::ArrayPtr<const uint8_t> StablePackageQueryKey::canonicalPackageBytes() const {
  return canonicalPackageBytesField.asPtr();
}

bool StablePackageQueryKey::operator==(const StablePackageQueryKey& other) const noexcept {
  return canonicalPackageBytes() == other.canonicalPackageBytes();
}

bool StablePackageQueryKey::operator<(const StablePackageQueryKey& other) const noexcept {
  return compareBytes(canonicalPackageBytes(), other.canonicalPackageBytes()) < 0;
}

PackageRootSetKey::PackageRootSetKey(zc::Vector<StablePackageQueryKey>&& packages) noexcept
    : packageFields(zc::mv(packages)) {}

zc::Maybe<PackageRootSetKey> PackageRootSetKey::fromVerified(
    const package::VerifiedPackageCompilationRequest& request) {
  zc::Vector<StablePackageQueryKey> packages(request.roots().size());
  for (const auto& root : request.roots()) {
    auto package = StablePackageQueryKey::fromVerified(root.packageKey());
    if (package == zc::none) { return zc::none; }
    ZC_IF_SOME(value, package) { packages.add(zc::mv(value)); }
  }
  return from(zc::mv(packages));
}

zc::Maybe<PackageRootSetKey> PackageRootSetKey::from(zc::Vector<StablePackageQueryKey>&& packages) {
  if (packages.size() == 0 || packages.size() > kMaximumPackageRoots) { return zc::none; }
  for (size_t index = 1; index < packages.size(); ++index) {
    auto current = zc::mv(packages[index]);
    size_t insertion = index;
    while (insertion != 0 && current < packages[insertion - 1]) {
      packages[insertion] = zc::mv(packages[insertion - 1]);
      --insertion;
    }
    packages[insertion] = zc::mv(current);
  }
  zc::Vector<StablePackageQueryKey> unique(packages.size());
  for (auto& package : packages) {
    if (unique.size() == 0 || unique.back() != package) { unique.add(zc::mv(package)); }
  }
  PackageRootSetKey result(zc::mv(unique));
  if (result.encodeCanonical().size() > kMaximumEncodedPackageOrCrateSetBytes) { return zc::none; }
  return zc::mv(result);
}

PackageRootSetKey PackageRootSetKey::clone() const {
  zc::Vector<StablePackageQueryKey> packages(packageFields.size());
  for (const auto& package : packageFields) { packages.add(package.clone()); }
  return PackageRootSetKey(zc::mv(packages));
}

zc::ArrayPtr<const StablePackageQueryKey> PackageRootSetKey::packages() const {
  return packageFields.asPtr();
}

bool PackageRootSetKey::operator==(const PackageRootSetKey& other) const noexcept {
  if (packageFields.size() != other.packageFields.size()) { return false; }
  for (size_t index = 0; index < packageFields.size(); ++index) {
    if (packageFields[index] != other.packageFields[index]) { return false; }
  }
  return true;
}

zc::Array<uint8_t> PackageRootSetKey::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(packageFields.size());
  for (const auto& package : packageFields) {
    encoder.encodeByteString(package.canonicalPackageBytes());
  }
  return encoder.finish();
}

zc::Maybe<PackageRootSetKey> PackageRootSetKey::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedPackageOrCrateSetBytes) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumPackageRoots);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<StablePackageQueryKey> packages(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumPackageOrCrateKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto package = StablePackageQueryKey::decodeBounded(value.asPtr());
      if (package == zc::none) { return zc::none; }
      ZC_IF_SOME(packageValue, package) {
        if (packages.size() != 0 && !(packages.back() < packageValue)) { return zc::none; }
        packages.add(zc::mv(packageValue));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return PackageRootSetKey(zc::mv(packages));
}

StableCrateQueryKey::StableCrateQueryKey(zc::Array<uint8_t>&& canonicalCrateBytes) noexcept
    : canonicalCrateBytesField(zc::mv(canonicalCrateBytes)) {}

zc::Maybe<StableCrateQueryKey> StableCrateQueryKey::fromVerified(const identity::CrateKey& crate) {
  return decodeBounded(crate.encode().asPtr());
}

zc::Maybe<StableCrateQueryKey> StableCrateQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumPackageOrCrateKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  if (crate == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, crate) {
    auto canonical = value.encode();
    if (canonical.asPtr() != bytes) { return zc::none; }
  }
  return StableCrateQueryKey(zc::heapArray<uint8_t>(bytes));
}

StableCrateQueryKey StableCrateQueryKey::clone() const {
  return StableCrateQueryKey(zc::heapArray<uint8_t>(canonicalCrateBytesField.asPtr()));
}

zc::ArrayPtr<const uint8_t> StableCrateQueryKey::canonicalCrateBytes() const {
  return canonicalCrateBytesField.asPtr();
}

bool StableCrateQueryKey::operator==(const StableCrateQueryKey& other) const noexcept {
  return canonicalCrateBytes() == other.canonicalCrateBytes();
}

bool StableCrateQueryKey::operator<(const StableCrateQueryKey& other) const noexcept {
  return compareBytes(canonicalCrateBytes(), other.canonicalCrateBytes()) < 0;
}

CompilationRootKey::CompilationRootKey(UserPackageCompilationRoot&& root) noexcept
    : value(zc::mv(root)) {}

CompilationRootKey::CompilationRootKey(ToolchainCoreCompilationRoot&& root) noexcept
    : value(zc::mv(root)) {}

zc::Maybe<CompilationRootKey> CompilationRootKey::userPackage(const identity::PackageKey& package) {
  auto stable = StablePackageQueryKey::fromVerified(package);
  if (stable == zc::none) { return zc::none; }
  return CompilationRootKey(UserPackageCompilationRoot{zc::mv(ZC_ASSERT_NONNULL(stable))});
}

zc::Maybe<CompilationRootKey> CompilationRootKey::toolchainCore(const identity::CrateKey& crate) {
  if (crate.unit().kind() != identity::CompilationUnitKind::Toolchain ||
      crate.unit().toolchain().component() != identity::ToolchainComponent::Core ||
      crate.targetKind() != identity::CrateTargetKind::Library || crate.targetName() != "core"_zc ||
      crate.compilation().hasBuildScriptProducer() ||
      crate.semanticOptions().editionYear() != 2026) {
    return zc::none;
  }
  auto stable = StableCrateQueryKey::fromVerified(crate);
  if (stable == zc::none) { return zc::none; }
  return CompilationRootKey(ToolchainCoreCompilationRoot{zc::mv(ZC_ASSERT_NONNULL(stable))});
}

CompilationRootKey CompilationRootKey::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(root, UserPackageCompilationRoot) {
      return CompilationRootKey(UserPackageCompilationRoot{root.package.clone()});
    }
    ZC_CASE_ONEOF(root, ToolchainCoreCompilationRoot) {
      return CompilationRootKey(ToolchainCoreCompilationRoot{root.crate.clone()});
    }
  }
  ZC_UNREACHABLE
}

CompilationRootKind CompilationRootKey::kind() const noexcept {
  if (value.is<UserPackageCompilationRoot>()) { return CompilationRootKind::UserPackage; }
  return CompilationRootKind::ToolchainCore;
}

const StablePackageQueryKey& CompilationRootKey::userPackage() const {
  return value.get<UserPackageCompilationRoot>().package;
}

const StableCrateQueryKey& CompilationRootKey::toolchainCore() const {
  return value.get<ToolchainCoreCompilationRoot>().crate;
}

zc::Array<uint8_t> CompilationRootKey::encodeCanonical() const {
  zc::ArrayPtr<const uint8_t> payload;
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(root, UserPackageCompilationRoot) {
      payload = root.package.canonicalPackageBytes();
    }
    ZC_CASE_ONEOF(root, ToolchainCoreCompilationRoot) {
      payload = root.crate.canonicalCrateBytes();
    }
  }
  auto encoded = zc::heapArray<uint8_t>(1 + payload.size());
  encoded[0] = static_cast<uint8_t>(kind());
  for (size_t index = 0; index < payload.size(); ++index) { encoded[index + 1] = payload[index]; }
  return encoded;
}

zc::Maybe<CompilationRootKey> CompilationRootKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() < 2 || bytes.size() > 1 + kMaximumPackageOrCrateKeyBytes) { return zc::none; }
  const auto payload = bytes.slice(1, bytes.size());
  switch (static_cast<CompilationRootKind>(bytes[0])) {
    case CompilationRootKind::UserPackage: {
      identity::CanonicalDecoder decoder(payload);
      auto package = identity::PackageKey::decodeCanonical(decoder);
      if (package == zc::none || !decoder.finished() ||
          ZC_ASSERT_NONNULL(package).encode().asPtr() != payload) {
        return zc::none;
      }
      return userPackage(ZC_ASSERT_NONNULL(package));
    }
    case CompilationRootKind::ToolchainCore: {
      identity::CanonicalDecoder decoder(payload);
      auto crate = identity::CrateKey::decodeCanonical(decoder);
      if (crate == zc::none || !decoder.finished() ||
          ZC_ASSERT_NONNULL(crate).encode().asPtr() != payload) {
        return zc::none;
      }
      return toolchainCore(ZC_ASSERT_NONNULL(crate));
    }
  }
  return zc::none;
}

bool CompilationRootKey::operator==(const CompilationRootKey& other) const noexcept {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

bool CompilationRootKey::operator<(const CompilationRootKey& other) const noexcept {
  const auto left = encodeCanonical();
  const auto right = other.encodeCanonical();
  return compareBytes(left.asPtr(), right.asPtr()) < 0;
}

CompilationRootSetQueryKey::CompilationRootSetQueryKey(
    zc::Vector<CompilationRootKey>&& roots) noexcept
    : rootFields(zc::mv(roots)) {}

zc::Maybe<CompilationRootSetQueryKey> CompilationRootSetQueryKey::fromVerified(
    const package::VerifiedPackageCompilationRequest& request) {
  return fromVerified(request, {});
}

zc::Maybe<CompilationRootSetQueryKey> CompilationRootSetQueryKey::fromVerified(
    const package::VerifiedPackageCompilationRequest& request,
    zc::ArrayPtr<const identity::CrateKey> projectedCoreCrates) {
  zc::Vector<CompilationRootKey> roots(request.roots().size() + projectedCoreCrates.size());
  for (const auto& root : request.roots()) {
    auto projected = CompilationRootKey::userPackage(root.packageKey());
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(value, projected) {
      bool present = false;
      for (const auto& existing : roots) {
        if (existing == value) {
          present = true;
          break;
        }
      }
      if (!present) { roots.add(zc::mv(value)); }
    }
  }
  for (const auto& crate : projectedCoreCrates) {
    auto projected = CompilationRootKey::toolchainCore(crate);
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(value, projected) { roots.add(zc::mv(value)); }
  }
  return from(zc::mv(roots));
}

zc::Maybe<CompilationRootSetQueryKey> CompilationRootSetQueryKey::singletonToolchainCore(
    const identity::CrateKey& crate) {
  auto projected = CompilationRootKey::toolchainCore(crate);
  if (projected == zc::none) { return zc::none; }
  zc::Vector<CompilationRootKey> roots;
  roots.add(zc::mv(ZC_ASSERT_NONNULL(projected)));
  return from(zc::mv(roots));
}

zc::Maybe<CompilationRootSetQueryKey> CompilationRootSetQueryKey::from(
    zc::Vector<CompilationRootKey>&& roots) {
  if (roots.size() == 0 || roots.size() > kMaximumCompilationRoots) { return zc::none; }
  for (size_t index = 1; index < roots.size(); ++index) {
    auto current = zc::mv(roots[index]);
    size_t insertion = index;
    while (insertion != 0 && current < roots[insertion - 1]) {
      roots[insertion] = zc::mv(roots[insertion - 1]);
      --insertion;
    }
    roots[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < roots.size(); ++index) {
    if (roots[index - 1] == roots[index]) { return zc::none; }
  }
  CompilationRootSetQueryKey result(zc::mv(roots));
  if (result.encodeCanonical().size() > kMaximumEncodedPackageOrCrateSetBytes) { return zc::none; }
  return zc::mv(result);
}

CompilationRootSetQueryKey CompilationRootSetQueryKey::clone() const {
  zc::Vector<CompilationRootKey> roots(rootFields.size());
  for (const auto& root : rootFields) { roots.add(root.clone()); }
  return CompilationRootSetQueryKey(zc::mv(roots));
}

zc::ArrayPtr<const CompilationRootKey> CompilationRootSetQueryKey::roots() const {
  return rootFields.asPtr();
}

zc::Array<uint8_t> CompilationRootSetQueryKey::encodeCanonical() const {
  identity::CanonicalEncoder tailEncoder;
  tailEncoder.encodeSequenceSize(rootFields.size());
  for (const auto& root : rootFields) {
    const auto encoded = root.encodeCanonical();
    tailEncoder.encodeByteString(encoded.asPtr());
  }
  const auto tail = tailEncoder.finish();
  auto encoded = zc::heapArray<uint8_t>(kCompilationRootSetDomain.size() + 1 + tail.size());
  size_t cursor = 0;
  for (const auto byte : kCompilationRootSetDomain.asBytes()) { encoded[cursor++] = byte; }
  encoded[cursor++] = 0;
  for (const auto byte : tail) { encoded[cursor++] = byte; }
  return encoded;
}

zc::Maybe<CompilationRootSetQueryKey> CompilationRootSetQueryKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  const size_t prefixSize = kCompilationRootSetDomain.size() + 1;
  if (bytes.size() <= prefixSize || bytes.size() > kMaximumEncodedPackageOrCrateSetBytes ||
      bytes.slice(0, kCompilationRootSetDomain.size()) != kCompilationRootSetDomain.asBytes() ||
      bytes[kCompilationRootSetDomain.size()] != 0) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(prefixSize, bytes.size()));
  auto count = decoder.decodeSequenceSize(kMaximumCompilationRoots);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<CompilationRootKey> roots(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(1 + kMaximumPackageOrCrateKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    auto root = CompilationRootKey::decodeCanonical(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (root == zc::none) { return zc::none; }
    if (roots.size() != 0 && !(roots.back() < ZC_ASSERT_NONNULL(root))) { return zc::none; }
    roots.add(zc::mv(ZC_ASSERT_NONNULL(root)));
  }
  if (!decoder.finished()) { return zc::none; }
  return CompilationRootSetQueryKey(zc::mv(roots));
}

bool CompilationRootSetQueryKey::operator==(
    const CompilationRootSetQueryKey& other) const noexcept {
  if (rootFields.size() != other.rootFields.size()) { return false; }
  for (size_t index = 0; index < rootFields.size(); ++index) {
    if (rootFields[index] != other.rootFields[index]) { return false; }
  }
  return true;
}

CanonicalCrateSet::CanonicalCrateSet(zc::Vector<StableCrateQueryKey>&& crates) noexcept
    : crateFields(zc::mv(crates)) {}

zc::Maybe<CanonicalCrateSet> CanonicalCrateSet::from(zc::Vector<StableCrateQueryKey>&& crates) {
  if (crates.size() == 0 || crates.size() > kMaximumActiveCrates) { return zc::none; }
  for (size_t index = 1; index < crates.size(); ++index) {
    auto current = zc::mv(crates[index]);
    size_t insertion = index;
    while (insertion != 0 && current < crates[insertion - 1]) {
      crates[insertion] = zc::mv(crates[insertion - 1]);
      --insertion;
    }
    crates[insertion] = zc::mv(current);
  }
  zc::Vector<StableCrateQueryKey> unique(crates.size());
  for (auto& crate : crates) {
    if (unique.size() == 0 || unique.back() != crate) { unique.add(zc::mv(crate)); }
  }
  CanonicalCrateSet result(zc::mv(unique));
  if (result.encodeCanonical().size() > kMaximumEncodedPackageOrCrateSetBytes) { return zc::none; }
  return zc::mv(result);
}

CanonicalCrateSet CanonicalCrateSet::clone() const {
  zc::Vector<StableCrateQueryKey> crates(crateFields.size());
  for (const auto& crate : crateFields) { crates.add(crate.clone()); }
  return CanonicalCrateSet(zc::mv(crates));
}

zc::ArrayPtr<const StableCrateQueryKey> CanonicalCrateSet::crates() const {
  return crateFields.asPtr();
}

bool CanonicalCrateSet::contains(const StableCrateQueryKey& crate) const noexcept {
  for (const auto& candidate : crateFields) {
    if (candidate == crate) { return true; }
  }
  return false;
}

bool CanonicalCrateSet::operator==(const CanonicalCrateSet& other) const noexcept {
  if (crateFields.size() != other.crateFields.size()) { return false; }
  for (size_t index = 0; index < crateFields.size(); ++index) {
    if (crateFields[index] != other.crateFields[index]) { return false; }
  }
  return true;
}

zc::Array<uint8_t> CanonicalCrateSet::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(crateFields.size());
  for (const auto& crate : crateFields) { encoder.encodeByteString(crate.canonicalCrateBytes()); }
  return encoder.finish();
}

zc::Maybe<CanonicalCrateSet> CanonicalCrateSet::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedPackageOrCrateSetBytes) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumActiveCrates);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<StableCrateQueryKey> crates(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumPackageOrCrateKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto crate = StableCrateQueryKey::decodeBounded(value.asPtr());
      if (crate == zc::none) { return zc::none; }
      ZC_IF_SOME(crateValue, crate) {
        if (crates.size() != 0 && !(crates.back() < crateValue)) { return zc::none; }
        crates.add(zc::mv(crateValue));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return CanonicalCrateSet(zc::mv(crates));
}

StableModuleQueryKey::StableModuleQueryKey(zc::Array<uint8_t>&& canonicalModuleBytes) noexcept
    : canonicalModuleBytesField(zc::mv(canonicalModuleBytes)) {}

zc::Maybe<StableModuleQueryKey> StableModuleQueryKey::fromVerified(
    const identity::ModuleKey& module) {
  return decodeBounded(module.encode().asPtr());
}

zc::Maybe<StableModuleQueryKey> StableModuleQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto module = identity::ModuleKey::decodeCanonical(decoder);
  if (module == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, module) {
    auto canonical = value.encode();
    if (canonical.asPtr() != bytes) { return zc::none; }
  }
  return StableModuleQueryKey(zc::heapArray<uint8_t>(bytes));
}

StableModuleQueryKey StableModuleQueryKey::clone() const {
  return StableModuleQueryKey(zc::heapArray<uint8_t>(canonicalModuleBytesField.asPtr()));
}

zc::ArrayPtr<const uint8_t> StableModuleQueryKey::canonicalModuleBytes() const {
  return canonicalModuleBytesField.asPtr();
}

bool StableModuleQueryKey::operator==(const StableModuleQueryKey& other) const noexcept {
  return canonicalModuleBytes() == other.canonicalModuleBytes();
}

bool StableModuleQueryKey::operator<(const StableModuleQueryKey& other) const noexcept {
  return compareBytes(canonicalModuleBytes(), other.canonicalModuleBytes()) < 0;
}

CanonicalSourceSet::CanonicalSourceSet(
    zc::Vector<identity::source_query::StableSourceQueryKey>&& sources) noexcept
    : sourceFields(zc::mv(sources)) {}

zc::Maybe<CanonicalSourceSet> CanonicalSourceSet::from(
    zc::Vector<identity::source_query::StableSourceQueryKey>&& sources) {
  if (sources.size() == 0 || sources.size() > kMaximumActiveSources) { return zc::none; }
  for (size_t index = 1; index < sources.size(); ++index) {
    auto current = zc::mv(sources[index]);
    size_t insertion = index;
    while (insertion != 0 && current < sources[insertion - 1]) {
      sources[insertion] = zc::mv(sources[insertion - 1]);
      --insertion;
    }
    sources[insertion] = zc::mv(current);
  }
  zc::Vector<identity::source_query::StableSourceQueryKey> unique(sources.size());
  for (auto& source : sources) {
    if (unique.size() == 0 || unique.back() != source) { unique.add(zc::mv(source)); }
  }
  CanonicalSourceSet result(zc::mv(unique));
  if (result.encodeCanonical().size() > kMaximumEncodedModuleSetBytes) { return zc::none; }
  return zc::mv(result);
}

CanonicalSourceSet CanonicalSourceSet::clone() const {
  zc::Vector<identity::source_query::StableSourceQueryKey> sources(sourceFields.size());
  for (const auto& source : sourceFields) { sources.add(source.clone()); }
  return CanonicalSourceSet(zc::mv(sources));
}

zc::ArrayPtr<const identity::source_query::StableSourceQueryKey> CanonicalSourceSet::sources()
    const {
  return sourceFields.asPtr();
}

bool CanonicalSourceSet::contains(
    const identity::source_query::StableSourceQueryKey& source) const noexcept {
  for (const auto& candidate : sourceFields) {
    if (candidate == source) { return true; }
  }
  return false;
}

bool CanonicalSourceSet::operator==(const CanonicalSourceSet& other) const noexcept {
  if (sourceFields.size() != other.sourceFields.size()) { return false; }
  for (size_t index = 0; index < sourceFields.size(); ++index) {
    if (sourceFields[index] != other.sourceFields[index]) { return false; }
  }
  return true;
}

zc::Array<uint8_t> CanonicalSourceSet::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(sourceFields.size());
  for (const auto& source : sourceFields) {
    encoder.encodeByteString(source.canonicalSourceBytes());
  }
  return encoder.finish();
}

zc::Maybe<CanonicalSourceSet> CanonicalSourceSet::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedModuleSetBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumActiveSources);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<identity::source_query::StableSourceQueryKey> sources(
      static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumSourceKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto source = identity::source_query::StableSourceQueryKey::decodeBounded(value.asPtr());
      if (source == zc::none) { return zc::none; }
      ZC_IF_SOME(sourceValue, source) {
        if (sources.size() != 0 && !(sources.back() < sourceValue)) { return zc::none; }
        sources.add(zc::mv(sourceValue));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return CanonicalSourceSet(zc::mv(sources));
}

zc::Array<uint8_t> UserPackageActiveSourcesInput::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalCrateBytes());
}

zc::Maybe<UserPackageActiveSourcesInput::Key> UserPackageActiveSourcesInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableCrateQueryKey::decodeBounded(bytes);
}

zc::Array<uint8_t> UserPackageActiveSourcesInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<UserPackageActiveSourcesInput::Value> UserPackageActiveSourcesInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalSourceSet::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveSources::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalCrateBytes());
}

zc::Maybe<ActiveSources::Key> ActiveSources::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return StableCrateQueryKey::decodeBounded(bytes);
}

zc::Array<uint8_t> ActiveSources::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveSources::Value> ActiveSources::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalSourceSet::decodeCanonical(bytes);
}

query::TypedQueryResult<ActiveSources::Value> ActiveSources::provide(query::QueryContext& context,
                                                                     const Key& key) {
  identity::CanonicalDecoder decoder(key.canonicalCrateBytes());
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  if (crate == zc::none || !decoder.finished()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  if (ZC_ASSERT_NONNULL(crate).unit().kind() == identity::CompilationUnitKind::UserPackage) {
    auto explicitSources = context.get<UserPackageActiveSourcesInput>(key);
    if (explicitSources.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(explicitSources.runtimeFailure());
    }
    if (explicitSources.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    return query::TypedQueryResult<Value>::value(explicitSources.value().clone());
  }
  if (ZC_ASSERT_NONNULL(crate).unit().kind() != identity::CompilationUnitKind::Toolchain ||
      ZC_ASSERT_NONNULL(crate).unit().toolchain().component() !=
          identity::ToolchainComponent::Core) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }

  auto distribution =
      context.get<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core());
  if (distribution.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(distribution.runtimeFailure());
  }
  if (distribution.kind() != query::QueryValueKind::Value ||
      ZC_ASSERT_NONNULL(crate).semanticOptions().editionYear() !=
          distribution.value().record().editionYear()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }

  zc::Vector<identity::source_query::StableSourceQueryKey> sources;
  for (const auto& file : distribution.value().record().files()) {
    auto source =
        identity::SourceFileKey::from(ZC_ASSERT_NONNULL(crate).clone(),
                                      identity::SourceOriginKey::coreFile(
                                          identity::ToolchainUnitKey::core(), file.path().clone()));
    auto stable = identity::source_query::StableSourceQueryKey::fromVerified(source);
    if (stable == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    sources.add(zc::mv(ZC_ASSERT_NONNULL(stable)));
  }
  for (size_t index = 0; index < sources.size(); ++index) {
    auto snapshot = context.get<identity::source_query::SourceSnapshotInput>(sources[index]);
    if (snapshot.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(snapshot.runtimeFailure());
    }
    if (snapshot.kind() != query::QueryValueKind::Value ||
        snapshot.value().contentDigest() != distribution.value().record().files()[index].digest()) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
  }
  auto result = CanonicalSourceSet::from(zc::mv(sources));
  if (result == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveSources::verify(query::QueryContext& context, const Key& key,
                           const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  identity::CanonicalDecoder decoder(key.canonicalCrateBytes());
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  if (crate == zc::none || !decoder.finished()) { return false; }
  if (ZC_ASSERT_NONNULL(crate).unit().kind() == identity::CompilationUnitKind::UserPackage) {
    auto explicitSources = context.get<UserPackageActiveSourcesInput>(key);
    return !explicitSources.isRuntimeFailure() &&
           explicitSources.kind() == query::QueryValueKind::Value &&
           explicitSources.value() == result.value();
  }
  if (ZC_ASSERT_NONNULL(crate).unit().kind() != identity::CompilationUnitKind::Toolchain ||
      ZC_ASSERT_NONNULL(crate).unit().toolchain().component() !=
          identity::ToolchainComponent::Core) {
    return false;
  }

  auto distribution =
      context.get<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core());
  if (distribution.isRuntimeFailure() || distribution.kind() != query::QueryValueKind::Value ||
      ZC_ASSERT_NONNULL(crate).semanticOptions().editionYear() !=
          distribution.value().record().editionYear()) {
    return false;
  }
  zc::Vector<identity::source_query::StableSourceQueryKey> sources;
  for (const auto& file : distribution.value().record().files()) {
    auto source =
        identity::SourceFileKey::from(ZC_ASSERT_NONNULL(crate).clone(),
                                      identity::SourceOriginKey::coreFile(
                                          identity::ToolchainUnitKey::core(), file.path().clone()));
    auto stable = identity::source_query::StableSourceQueryKey::fromVerified(source);
    if (stable == zc::none) { return false; }
    sources.add(zc::mv(ZC_ASSERT_NONNULL(stable)));
  }
  for (size_t index = 0; index < sources.size(); ++index) {
    auto snapshot = context.get<identity::source_query::SourceSnapshotInput>(sources[index]);
    if (snapshot.isRuntimeFailure() || snapshot.kind() != query::QueryValueKind::Value ||
        snapshot.value().contentDigest() != distribution.value().record().files()[index].digest()) {
      return false;
    }
  }
  auto expected = CanonicalSourceSet::from(zc::mv(sources));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == result.value();
}

zc::Array<uint8_t> ActiveCrates::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<ActiveCrates::Key> ActiveCrates::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return CompilationRootSetQueryKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveCrates::encodeValue(const Value& value) { return value.encodeCanonical(); }

zc::Maybe<ActiveCrates::Value> ActiveCrates::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalCrateSet::decodeCanonical(bytes);
}

query::TypedQueryResult<ActiveCrates::Value> ActiveCrates::provide(query::QueryContext& context,
                                                                   const Key& key) {
  zc::Vector<StablePackageQueryKey> packageRoots;
  zc::Vector<StableCrateQueryKey> crates;
  zc::Vector<StableCrateQueryKey> coreCrates;
  for (const auto& root : key.roots()) {
    switch (root.kind()) {
      case CompilationRootKind::UserPackage:
        packageRoots.add(root.userPackage().clone());
        break;
      case CompilationRootKind::ToolchainCore:
        coreCrates.add(root.toolchainCore().clone());
        break;
    }
  }

  if (!packageRoots.empty()) {
    auto packageKey = PackageRootSetKey::from(zc::mv(packageRoots));
    if (packageKey == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    auto packageGraph = context.get<PackageGraphInput>(ZC_ASSERT_NONNULL(packageKey));
    if (packageGraph.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(packageGraph.runtimeFailure());
    }
    if (packageGraph.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    for (const auto& crate : packageGraph.value().crates()) { crates.add(crate.clone()); }
  }

  if (!coreCrates.empty()) {
    auto distribution =
        context.get<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core());
    if (distribution.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(distribution.runtimeFailure());
    }
    if (distribution.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    for (const auto& stable : coreCrates) {
      identity::CanonicalDecoder decoder(stable.canonicalCrateBytes());
      auto crate = identity::CrateKey::decodeCanonical(decoder);
      if (crate == zc::none || !decoder.finished() ||
          ZC_ASSERT_NONNULL(crate).semanticOptions().editionYear() !=
              distribution.value().record().editionYear()) {
        return query::TypedQueryResult<Value>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      crates.add(stable.clone());
    }
  }

  auto result = CanonicalCrateSet::from(zc::mv(crates));
  if (result == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ActiveCrates::verify(query::QueryContext& context, const Key& key,
                          const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }

  zc::Vector<StablePackageQueryKey> packageRoots;
  zc::Vector<StableCrateQueryKey> expectedCrates;
  zc::Vector<StableCrateQueryKey> coreCrates;
  for (const auto& root : key.roots()) {
    if (root.kind() == CompilationRootKind::UserPackage) {
      packageRoots.add(root.userPackage().clone());
    } else if (root.kind() == CompilationRootKind::ToolchainCore) {
      coreCrates.add(root.toolchainCore().clone());
    } else {
      return false;
    }
  }

  if (!packageRoots.empty()) {
    auto packageKey = PackageRootSetKey::from(zc::mv(packageRoots));
    if (packageKey == zc::none) { return false; }
    auto packageGraph = context.get<PackageGraphInput>(ZC_ASSERT_NONNULL(packageKey));
    if (packageGraph.isRuntimeFailure() || packageGraph.kind() != query::QueryValueKind::Value) {
      return false;
    }
    for (const auto& crate : packageGraph.value().crates()) { expectedCrates.add(crate.clone()); }
  }

  if (!coreCrates.empty()) {
    auto distribution =
        context.get<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core());
    if (distribution.isRuntimeFailure() || distribution.kind() != query::QueryValueKind::Value) {
      return false;
    }
    for (const auto& stable : coreCrates) {
      identity::CanonicalDecoder decoder(stable.canonicalCrateBytes());
      auto crate = identity::CrateKey::decodeCanonical(decoder);
      if (crate == zc::none || !decoder.finished() ||
          ZC_ASSERT_NONNULL(crate).semanticOptions().editionYear() !=
              distribution.value().record().editionYear()) {
        return false;
      }
      expectedCrates.add(stable.clone());
    }
  }

  auto expected = CanonicalCrateSet::from(zc::mv(expectedCrates));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == result.value();
}

bool registerIncrementalBindingQueryAdapter(query::QueryDatabase& database) {
  if (!registerIncrementalPackageGraphQueryInput(database)) { return false; }
  if (!incremental_module_resolution_query::registerIncrementalModuleResolutionQueries(database)) {
    return false;
  }
  if (!database.registerDescriptor<UserPackageActiveSourcesInput>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<ActiveSources>().isRegistered()) { return false; }
  if (!identity::source_query::registerSourceQueryInputs(database)) { return false; }
  if (!parser::registerParseSourceQuery(database)) { return false; }
  if (!database.registerDescriptor<ActiveCrates>().isRegistered()) { return false; }
  if (!registerActiveIdentityMembershipQueries(database)) { return false; }
  if (!database.registerDescriptor<IdentitySyntaxSiteInventoryQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<StableIdentityAdmissionQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<NamedDefinitionInventoryQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<NamedImplementationInventoryQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<RevisionLocalDefinitionSitesQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<RevisionLocalImplementationSitesQuery>().isRegistered()) {
    return false;
  }
  if (!binder::registerStableHeaderSyntaxQueries(database)) { return false; }
  if (!database.registerDescriptor<NamedItemSyntaxQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<NamedItemProvenanceQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ModuleBodySyntaxQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ModuleBodyProvenanceQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ModuleBodyOwnersQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<OwnerBodySyntaxQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<OwnerBodyProvenanceQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<binder::BindOwnerBody>().isRegistered()) { return false; }
  if (!database.registerDescriptor<binder::ModuleBindingAllocationPlanQuery>().isRegistered()) {
    return false;
  }
  return true;
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
