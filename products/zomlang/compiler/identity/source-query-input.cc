// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/identity/source-query-input.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/crate-key.h"
#include "zomlang/compiler/identity/source-key.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::identity::source_query {
namespace {

constexpr uint64_t kMaximumSourceKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumSourceSnapshotBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumTargetSelectionBytes = 32 * 1024;

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

query::QueryKindContract inputContract(zc::StringPtr domain, query::Durability durability) {
  auto contract = query::QueryKindContract::input(domain, durability);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

bool isValidTargetProfile(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > 255) { return false; }
  for (const auto value : bytes) {
    if ((value < 'a' || value > 'z') && (value < '0' || value > '9') && value != '.' &&
        value != '_' && value != '-') {
      return false;
    }
  }
  return true;
}

bool isZeroDigest(const Sha256Digest& digest) {
  for (const auto value : digest.bytes()) {
    if (value != 0) { return false; }
  }
  return true;
}

zc::Maybe<Sha256Digest> validateTargetSelection(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumTargetSelectionBytes) { return zc::none; }
  CanonicalDecoder decoder(bytes);
  auto registryRevision = decoder.decodeDigest();
  auto profile = decoder.decodeByteString(255);
  auto target = CanonicalTargetSpecificationKey::decodeCanonical(decoder);
  auto panic = decoder.decodeUint8();
  const auto abortPanic = static_cast<uint8_t>(driver::package::PackagePanicStrategy::Abort);
  const auto unwindPanic = static_cast<uint8_t>(driver::package::PackagePanicStrategy::Unwind);
  if (registryRevision == zc::none || profile == zc::none || target == zc::none ||
      panic == zc::none || !decoder.finished() ||
      !isValidTargetProfile(ZC_ASSERT_NONNULL(profile).asPtr()) ||
      isZeroDigest(ZC_ASSERT_NONNULL(registryRevision)) ||
      (ZC_ASSERT_NONNULL(panic) != abortPanic && ZC_ASSERT_NONNULL(panic) != unwindPanic)) {
    return zc::none;
  }
  return ZC_ASSERT_NONNULL(registryRevision);
}

bool selectionMatchesTarget(zc::ArrayPtr<const uint8_t> bytes,
                            const CanonicalTargetSpecificationKey& expected) {
  CanonicalDecoder decoder(bytes);
  auto registryRevision = decoder.decodeDigest();
  auto profile = decoder.decodeByteString(255);
  auto target = CanonicalTargetSpecificationKey::decodeCanonical(decoder);
  auto panic = decoder.decodeUint8();
  if (registryRevision == zc::none || profile == zc::none || target == zc::none ||
      panic == zc::none || !decoder.finished()) {
    return false;
  }
  CanonicalEncoder selectedEncoder;
  ZC_ASSERT_NONNULL(target).encode(selectedEncoder);
  const auto selectedBytes = selectedEncoder.finish();
  CanonicalEncoder expectedEncoder;
  expected.encode(expectedEncoder);
  return selectedBytes.asPtr() == expectedEncoder.finish().asPtr();
}

}  // namespace

StableSourceQueryKey::StableSourceQueryKey(zc::Array<uint8_t>&& canonicalSourceBytes) noexcept
    : canonicalSourceBytesField(zc::mv(canonicalSourceBytes)) {}

zc::Maybe<StableSourceQueryKey> StableSourceQueryKey::fromVerified(const SourceFileKey& source) {
  return decodeBounded(source.encode().asPtr());
}

zc::Maybe<StableSourceQueryKey> StableSourceQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumSourceKeyBytes) { return zc::none; }
  CanonicalDecoder decoder(bytes);
  auto source = SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(source).encode().asPtr() != bytes) {
    return zc::none;
  }
  return StableSourceQueryKey(zc::heapArray<uint8_t>(bytes));
}

StableSourceQueryKey StableSourceQueryKey::clone() const {
  return StableSourceQueryKey(zc::heapArray<uint8_t>(canonicalSourceBytesField.asPtr()));
}
zc::ArrayPtr<const uint8_t> StableSourceQueryKey::canonicalSourceBytes() const {
  return canonicalSourceBytesField.asPtr();
}
bool StableSourceQueryKey::operator==(const StableSourceQueryKey& other) const noexcept {
  return canonicalSourceBytes() == other.canonicalSourceBytes();
}
bool StableSourceQueryKey::operator<(const StableSourceQueryKey& other) const noexcept {
  return compareBytes(canonicalSourceBytes(), other.canonicalSourceBytes()) < 0;
}

CanonicalSourceSnapshot::CanonicalSourceSnapshot(const Sha256Digest& contentDigest,
                                                 zc::Array<uint8_t>&& bytes) noexcept
    : contentDigestField(contentDigest), bytesField(zc::mv(bytes)) {}

zc::Maybe<CanonicalSourceSnapshot> CanonicalSourceSnapshot::fromVerified(
    const ImmutableSourceSnapshot& snapshot) {
  if (snapshot.bytes().size() > kMaximumSourceSnapshotBytes) { return zc::none; }
  auto digest = sha256(snapshot.bytes());
  if (digest == zc::none || ZC_ASSERT_NONNULL(digest) != snapshot.contentDigest()) {
    return zc::none;
  }
  return CanonicalSourceSnapshot(ZC_ASSERT_NONNULL(digest),
                                 zc::heapArray<uint8_t>(snapshot.bytes()));
}
CanonicalSourceSnapshot CanonicalSourceSnapshot::clone() const {
  return CanonicalSourceSnapshot(contentDigestField, zc::heapArray<uint8_t>(bytesField.asPtr()));
}
const Sha256Digest& CanonicalSourceSnapshot::contentDigest() const noexcept {
  return contentDigestField;
}
zc::ArrayPtr<const uint8_t> CanonicalSourceSnapshot::bytes() const { return bytesField.asPtr(); }
bool CanonicalSourceSnapshot::operator==(const CanonicalSourceSnapshot& other) const noexcept {
  return contentDigestField == other.contentDigestField && bytes() == other.bytes();
}
zc::Array<uint8_t> CanonicalSourceSnapshot::encodeCanonical() const {
  CanonicalEncoder encoder;
  encoder.encodeDigest(contentDigestField);
  encoder.encodeByteString(bytes());
  return encoder.finish();
}
zc::Maybe<CanonicalSourceSnapshot> CanonicalSourceSnapshot::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() > 32 + 8 + kMaximumSourceSnapshotBytes) { return zc::none; }
  CanonicalDecoder decoder(encoded);
  auto digest = decoder.decodeDigest();
  auto bytes = decoder.decodeByteString(kMaximumSourceSnapshotBytes);
  if (digest == zc::none || bytes == zc::none || !decoder.finished()) { return zc::none; }
  auto computed = sha256(ZC_ASSERT_NONNULL(bytes).asPtr());
  if (computed == zc::none || ZC_ASSERT_NONNULL(computed) != ZC_ASSERT_NONNULL(digest)) {
    return zc::none;
  }
  return CanonicalSourceSnapshot(ZC_ASSERT_NONNULL(digest), zc::mv(ZC_ASSERT_NONNULL(bytes)));
}

CanonicalCompilationOptions::CanonicalCompilationOptions(zc::Array<uint8_t>&& hostTarget,
                                                         zc::Array<uint8_t>&& target,
                                                         bool useUnicode,
                                                         bool allowDollarIdentifiers,
                                                         bool supportRegexLiterals) noexcept
    : hostTargetField(zc::mv(hostTarget)),
      targetField(zc::mv(target)),
      useUnicodeField(useUnicode),
      allowDollarIdentifiersField(allowDollarIdentifiers),
      supportRegexLiteralsField(supportRegexLiterals) {}

zc::Maybe<CanonicalCompilationOptions> CanonicalCompilationOptions::fromVerified(
    const driver::package::VerifiedPackageCompilationRequest& request) {
  CanonicalEncoder hostEncoder;
  request.hostTarget().encode(hostEncoder);
  auto host = hostEncoder.finish();
  CanonicalEncoder targetEncoder;
  request.target().encode(targetEncoder);
  auto target = targetEncoder.finish();
  return fromCanonicalSelections(zc::mv(host), zc::mv(target), request.languageOptions().useUnicode,
                                 request.languageOptions().allowDollarIdentifiers,
                                 request.languageOptions().supportRegexLiterals);
}
zc::Maybe<CanonicalCompilationOptions> CanonicalCompilationOptions::fromCanonicalSelections(
    zc::Array<uint8_t>&& host, zc::Array<uint8_t>&& target, bool useUnicode,
    bool allowDollarIdentifiers, bool supportRegexLiterals) {
  auto hostRevision = validateTargetSelection(host.asPtr());
  auto targetRevision = validateTargetSelection(target.asPtr());
  if (hostRevision == zc::none || targetRevision == zc::none ||
      ZC_ASSERT_NONNULL(hostRevision) != ZC_ASSERT_NONNULL(targetRevision)) {
    return zc::none;
  }
  return CanonicalCompilationOptions(zc::mv(host), zc::mv(target), useUnicode,
                                     allowDollarIdentifiers, supportRegexLiterals);
}
CanonicalCompilationOptions CanonicalCompilationOptions::clone() const {
  return CanonicalCompilationOptions(zc::heapArray<uint8_t>(hostTargetField.asPtr()),
                                     zc::heapArray<uint8_t>(targetField.asPtr()), useUnicodeField,
                                     allowDollarIdentifiersField, supportRegexLiteralsField);
}
zc::ArrayPtr<const uint8_t> CanonicalCompilationOptions::hostTargetBytes() const {
  return hostTargetField.asPtr();
}
zc::ArrayPtr<const uint8_t> CanonicalCompilationOptions::targetBytes() const {
  return targetField.asPtr();
}
bool CanonicalCompilationOptions::useUnicode() const noexcept { return useUnicodeField; }
bool CanonicalCompilationOptions::allowDollarIdentifiers() const noexcept {
  return allowDollarIdentifiersField;
}
bool CanonicalCompilationOptions::supportRegexLiterals() const noexcept {
  return supportRegexLiteralsField;
}
bool CanonicalCompilationOptions::matchesCrate(const CrateKey& crate) const {
  const auto& semantic = crate.semanticOptions();
  const auto selectedTarget = crate.compilation().domain() == CompilationDomain::Host
                                  ? hostTargetField.asPtr()
                                  : targetField.asPtr();
  return selectionMatchesTarget(selectedTarget, crate.compilation().target()) &&
         useUnicodeField == semantic.useUnicode() &&
         allowDollarIdentifiersField == semantic.allowDollarIdentifiers() &&
         supportRegexLiteralsField == semantic.supportRegexLiterals();
}
bool CanonicalCompilationOptions::operator==(
    const CanonicalCompilationOptions& other) const noexcept {
  return hostTargetField.asPtr() == other.hostTargetField.asPtr() &&
         targetField.asPtr() == other.targetField.asPtr() &&
         useUnicodeField == other.useUnicodeField &&
         allowDollarIdentifiersField == other.allowDollarIdentifiersField &&
         supportRegexLiteralsField == other.supportRegexLiteralsField;
}
zc::Array<uint8_t> CanonicalCompilationOptions::encodeCanonical() const {
  CanonicalEncoder encoder;
  encoder.encodeByteString(hostTargetField.asPtr());
  encoder.encodeByteString(targetField.asPtr());
  encoder.encodeBool(useUnicodeField);
  encoder.encodeBool(allowDollarIdentifiersField);
  encoder.encodeBool(supportRegexLiteralsField);
  return encoder.finish();
}
zc::Maybe<CanonicalCompilationOptions> CanonicalCompilationOptions::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > 2 * (8 + kMaximumTargetSelectionBytes) + 3) { return zc::none; }
  CanonicalDecoder decoder(bytes);
  auto host = decoder.decodeByteString(kMaximumTargetSelectionBytes);
  auto target = decoder.decodeByteString(kMaximumTargetSelectionBytes);
  auto useUnicode = decoder.decodeBool();
  auto allowDollarIdentifiers = decoder.decodeBool();
  auto supportRegexLiterals = decoder.decodeBool();
  if (host == zc::none || target == zc::none || useUnicode == zc::none ||
      allowDollarIdentifiers == zc::none || supportRegexLiterals == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto hostRevision = validateTargetSelection(ZC_ASSERT_NONNULL(host).asPtr());
  auto targetRevision = validateTargetSelection(ZC_ASSERT_NONNULL(target).asPtr());
  if (hostRevision == zc::none || targetRevision == zc::none ||
      ZC_ASSERT_NONNULL(hostRevision) != ZC_ASSERT_NONNULL(targetRevision)) {
    return zc::none;
  }
  return CanonicalCompilationOptions(
      zc::mv(ZC_ASSERT_NONNULL(host)), zc::mv(ZC_ASSERT_NONNULL(target)),
      ZC_ASSERT_NONNULL(useUnicode), ZC_ASSERT_NONNULL(allowDollarIdentifiers),
      ZC_ASSERT_NONNULL(supportRegexLiterals));
}

zc::StringPtr CompilationOptionsInput::domain() { return "zom.query.compilation-options"_zc; }
query::QueryKindContract CompilationOptionsInput::contract() {
  return inputContract(domain(), query::Durability::Medium);
}
zc::Array<uint8_t> CompilationOptionsInput::encodeKey(const Key& key) { return key.encode(); }
zc::Maybe<CompilationOptionsInput::Key> CompilationOptionsInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  CanonicalDecoder decoder(bytes);
  auto key = CrateKey::decodeCanonical(decoder);
  if (key == zc::none || !decoder.finished() || ZC_ASSERT_NONNULL(key).encode().asPtr() != bytes) {
    return zc::none;
  }
  return key;
}
zc::Array<uint8_t> CompilationOptionsInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}
zc::Maybe<CompilationOptionsInput::Value> CompilationOptionsInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalCompilationOptions::decodeCanonical(bytes);
}

zc::StringPtr SourceSnapshotInput::domain() { return "zom.query.source-snapshot"_zc; }
query::QueryKindContract SourceSnapshotInput::contract() {
  return inputContract(domain(), query::Durability::Low);
}
zc::Array<uint8_t> SourceSnapshotInput::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalSourceBytes());
}
zc::Maybe<SourceSnapshotInput::Key> SourceSnapshotInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableSourceQueryKey::decodeBounded(bytes);
}
zc::Array<uint8_t> SourceSnapshotInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}
zc::Maybe<SourceSnapshotInput::Value> SourceSnapshotInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalSourceSnapshot::decodeCanonical(bytes);
}

bool registerSourceQueryInputs(query::QueryDatabase& database) {
  return database.registerInputKind<CompilationOptionsInput>() != zc::none &&
         database.registerInputKind<SourceSnapshotInput>() != zc::none;
}

}  // namespace zomlang::compiler::identity::source_query
