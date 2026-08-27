// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/package/canonical-package-compilation-request.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {

// Canonical wire foundation and compilation root record.

namespace {

constexpr zc::StringPtr kCompilationRootDomain = "zom.input.canonical-compilation-root"_zc;
constexpr zc::StringPtr kTargetSelectionDomain = "zom.input.canonical-target-selection"_zc;
constexpr zc::StringPtr kLanguageOptionsDomain = "zom.input.canonical-language-options"_zc;
constexpr zc::StringPtr kPackageRequestDomain =
    "zom.input.canonical-package-compilation-request"_zc;
constexpr uint64_t kMaximumSchemaCount = UINT32_MAX;
constexpr uint64_t kMaximumProfileBytes = 255;
constexpr size_t kEncodedCountBytes = sizeof(uint64_t);

bool valid(identity::CrateTargetKind value) {
  return value >= identity::CrateTargetKind::Library &&
         value <= identity::CrateTargetKind::BuildScript;
}
bool valid(PackagePanicStrategy value) {
  return value == PackagePanicStrategy::Abort || value == PackagePanicStrategy::Unwind;
}
bool valid(PackageLockMode value) {
  return value == PackageLockMode::LockedOnly || value == PackageLockMode::PreferLocked ||
         value == PackageLockMode::Update;
}
bool zero(const identity::Sha256Digest& digest) {
  for (const auto byte : digest.bytes()) {
    if (byte != 0) { return false; }
  }
  return true;
}

bool addSize(size_t& total, size_t amount) {
  if (amount > SIZE_MAX - total) { return false; }
  total += amount;
  return true;
}
bool beginRecordSize(zc::StringPtr domain, size_t& total) {
  total = 0;
  return addSize(total, domain.size()) && addSize(total, 1);
}
bool addFramedSize(size_t& total, size_t payloadSize) {
  return payloadSize <= UINT32_MAX && addSize(total, kEncodedCountBytes) &&
         addSize(total, payloadSize);
}

zc::Array<uint8_t> frame(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> payload) {
  size_t encodedSize;
  ZC_REQUIRE(beginRecordSize(domain, encodedSize) && addSize(encodedSize, payload.size()),
             "canonical record size must fit size_t");
  auto result = zc::heapArray<uint8_t>(encodedSize);
  size_t cursor = 0;
  for (const auto byte : domain.asBytes()) { result[cursor++] = byte; }
  result[cursor++] = 0;
  for (const auto byte : payload) { result[cursor++] = byte; }
  return result;
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> unframe(zc::StringPtr domain,
                                               zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() <= domain.size() || bytes.slice(0, domain.size()) != domain.asBytes() ||
      bytes[domain.size()] != 0) {
    return zc::none;
  }
  return bytes.slice(domain.size() + 1, bytes.size());
}

zc::Maybe<zc::Array<uint8_t>> encodeLiveCompilationRootProjection(
    const VerifiedCompilationRoot& root) {
  if (!valid(root.targetKind()) || root.sourcePath().segments().size() > UINT32_MAX ||
      identity::TargetName::fromCanonical(root.targetName()) == zc::none) {
    return zc::none;
  }
  auto package = root.packageKey().encode();
  if (package.size() > UINT32_MAX) { return zc::none; }
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(package.asPtr());
  encoder.encodeUint8(static_cast<uint8_t>(root.targetKind()));
  encoder.encodeByteString(root.targetName().asBytes());
  encoder.encodeUint32(root.editionYear());
  encoder.encodeBool(root.requiresBuildScript());
  root.sourcePath().encode(encoder);
  auto payload = encoder.finish();
  size_t encodedSize;
  if (!beginRecordSize(kCompilationRootDomain, encodedSize) ||
      !addSize(encodedSize, payload.size()) || encodedSize > UINT32_MAX) {
    return zc::none;
  }
  return frame(kCompilationRootDomain, payload.asPtr());
}

}  // namespace

struct CanonicalCompilationRootRecord::Impl final {
  Impl(identity::PackageKey&& package, identity::CrateTargetKind targetKind,
       identity::TargetName&& targetName, uint32_t editionYear, bool requiresBuildScript,
       identity::CanonicalRelativePath&& sourcePath) noexcept
      : package(zc::mv(package)),
        targetKind(targetKind),
        targetName(zc::mv(targetName)),
        editionYear(editionYear),
        requiresBuildScript(requiresBuildScript),
        sourcePath(zc::mv(sourcePath)) {}
  identity::PackageKey package;
  identity::CrateTargetKind targetKind;
  identity::TargetName targetName;
  uint32_t editionYear;
  bool requiresBuildScript;
  identity::CanonicalRelativePath sourcePath;
};

CanonicalCompilationRootRecord::CanonicalCompilationRootRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalCompilationRootRecord::~CanonicalCompilationRootRecord() noexcept(false) = default;
CanonicalCompilationRootRecord::CanonicalCompilationRootRecord(
    CanonicalCompilationRootRecord&&) noexcept = default;
CanonicalCompilationRootRecord& CanonicalCompilationRootRecord::operator=(
    CanonicalCompilationRootRecord&&) noexcept = default;

zc::Maybe<CanonicalCompilationRootRecord> CanonicalCompilationRootRecord::project(
    const VerifiedCompilationRoot& root) {
  auto encoded = encodeLiveCompilationRootProjection(root);
  auto targetName = identity::TargetName::fromCanonical(root.targetName());
  if (encoded == zc::none || targetName == zc::none) { return zc::none; }
  return CanonicalCompilationRootRecord(zc::heap<Impl>(
      root.packageKey().clone(), root.targetKind(), zc::mv(ZC_ASSERT_NONNULL(targetName)),
      root.editionYear(), root.requiresBuildScript(), root.sourcePath().clone()));
}

zc::Maybe<CanonicalCompilationRootRecord> CanonicalCompilationRootRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > UINT32_MAX) { return zc::none; }
  auto payload = unframe(kCompilationRootDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto packageBytes = decoder.decodeByteString(kMaximumSchemaCount);
  if (packageBytes == zc::none) { return zc::none; }
  identity::CanonicalDecoder packageDecoder(ZC_ASSERT_NONNULL(packageBytes).asPtr());
  auto package = identity::PackageKey::decodeCanonical(packageDecoder);
  auto kind = decoder.decodeUint8();
  auto targetName = identity::TargetName::decodeCanonical(decoder);
  auto editionYear = decoder.decodeUint32();
  auto requiresBuildScript = decoder.decodeBool();
  auto sourcePath = identity::CanonicalRelativePath::decodeCanonical(decoder);
  if (package == zc::none || !packageDecoder.finished() || kind == zc::none ||
      targetName == zc::none || editionYear == zc::none || requiresBuildScript == zc::none ||
      sourcePath == zc::none || !decoder.finished() ||
      !valid(static_cast<identity::CrateTargetKind>(ZC_ASSERT_NONNULL(kind)))) {
    return zc::none;
  }
  CanonicalCompilationRootRecord result(zc::heap<Impl>(
      zc::mv(ZC_ASSERT_NONNULL(package)),
      static_cast<identity::CrateTargetKind>(ZC_ASSERT_NONNULL(kind)),
      zc::mv(ZC_ASSERT_NONNULL(targetName)), ZC_ASSERT_NONNULL(editionYear),
      ZC_ASSERT_NONNULL(requiresBuildScript), zc::mv(ZC_ASSERT_NONNULL(sourcePath))));
  if (result.encodeCanonical().asPtr() != bytes) { return zc::none; }
  return result;
}

CanonicalCompilationRootRecord CanonicalCompilationRootRecord::clone() const {
  return CanonicalCompilationRootRecord(
      zc::heap<Impl>(impl->package.clone(), impl->targetKind, impl->targetName.clone(),
                     impl->editionYear, impl->requiresBuildScript, impl->sourcePath.clone()));
}
const identity::PackageKey& CanonicalCompilationRootRecord::package() const noexcept {
  return impl->package;
}
identity::CrateTargetKind CanonicalCompilationRootRecord::targetKind() const noexcept {
  return impl->targetKind;
}
zc::StringPtr CanonicalCompilationRootRecord::targetName() const noexcept {
  return impl->targetName.text();
}
uint32_t CanonicalCompilationRootRecord::editionYear() const noexcept { return impl->editionYear; }
bool CanonicalCompilationRootRecord::requiresBuildScript() const noexcept {
  return impl->requiresBuildScript;
}
const identity::CanonicalRelativePath& CanonicalCompilationRootRecord::sourcePath() const noexcept {
  return impl->sourcePath;
}
zc::Array<uint8_t> CanonicalCompilationRootRecord::encodeCanonical() const {
  auto package = impl->package.encode();
  ZC_IREQUIRE(package.size() <= UINT32_MAX, "canonical package key must fit RFC byte bound");
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(package.asPtr());
  encoder.encodeUint8(static_cast<uint8_t>(impl->targetKind));
  impl->targetName.encode(encoder);
  encoder.encodeUint32(impl->editionYear);
  encoder.encodeBool(impl->requiresBuildScript);
  impl->sourcePath.encode(encoder);
  auto payload = encoder.finish();
  size_t encodedSize;
  ZC_REQUIRE(beginRecordSize(kCompilationRootDomain, encodedSize) &&
                 addSize(encodedSize, payload.size()) && encodedSize <= UINT32_MAX,
             "canonical compilation root must fit RFC byte bound");
  return frame(kCompilationRootDomain, payload.asPtr());
}
bool CanonicalCompilationRootRecord::operator==(const CanonicalCompilationRootRecord& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

// Canonical target selection and language option records.

namespace {

zc::Maybe<zc::Array<uint8_t>> encodeLiveTargetSelectionProjection(
    const RegisteredTargetSelection& selection) {
  if (zero(selection.registryRevision()) || !valid(selection.panicStrategy()) ||
      RegisteredTargetProfileName::from(selection.profile()) == zc::none) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(selection.registryRevision());
  encoder.encodeByteString(selection.profile().asBytes());
  selection.semanticProjection().encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(selection.panicStrategy()));
  auto payload = encoder.finish();
  size_t encodedSize;
  if (!beginRecordSize(kTargetSelectionDomain, encodedSize) ||
      !addSize(encodedSize, payload.size()) || encodedSize > UINT32_MAX) {
    return zc::none;
  }
  return frame(kTargetSelectionDomain, payload.asPtr());
}

zc::Array<uint8_t> encodeLiveLanguageOptionsProjection(const SelectedLanguageOptions& options) {
  identity::CanonicalEncoder encoder;
  encoder.encodeBool(options.useUnicode);
  encoder.encodeBool(options.allowDollarIdentifiers);
  encoder.encodeBool(options.supportRegexLiterals);
  return frame(kLanguageOptionsDomain, encoder.finish().asPtr());
}

}  // namespace

struct CanonicalTargetSelectionRecord::Impl final {
  Impl(const identity::Sha256Digest& registryRevision, RegisteredTargetProfileName&& profile,
       identity::CanonicalTargetSpecificationKey&& semanticProjection,
       PackagePanicStrategy panicStrategy) noexcept
      : registryRevision(registryRevision),
        profile(zc::mv(profile)),
        semanticProjection(zc::mv(semanticProjection)),
        panicStrategy(panicStrategy) {}
  identity::Sha256Digest registryRevision;
  RegisteredTargetProfileName profile;
  identity::CanonicalTargetSpecificationKey semanticProjection;
  PackagePanicStrategy panicStrategy;
};

CanonicalTargetSelectionRecord::CanonicalTargetSelectionRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalTargetSelectionRecord::~CanonicalTargetSelectionRecord() noexcept(false) = default;
CanonicalTargetSelectionRecord::CanonicalTargetSelectionRecord(
    CanonicalTargetSelectionRecord&&) noexcept = default;
CanonicalTargetSelectionRecord& CanonicalTargetSelectionRecord::operator=(
    CanonicalTargetSelectionRecord&&) noexcept = default;

zc::Maybe<CanonicalTargetSelectionRecord> CanonicalTargetSelectionRecord::project(
    const RegisteredTargetSelection& selection) {
  auto encoded = encodeLiveTargetSelectionProjection(selection);
  auto profile = RegisteredTargetProfileName::from(selection.profile());
  if (encoded == zc::none || profile == zc::none) { return zc::none; }
  return CanonicalTargetSelectionRecord(
      zc::heap<Impl>(selection.registryRevision(), zc::mv(ZC_ASSERT_NONNULL(profile)),
                     selection.semanticProjection().clone(), selection.panicStrategy()));
}

zc::Maybe<CanonicalTargetSelectionRecord> CanonicalTargetSelectionRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > UINT32_MAX) { return zc::none; }
  auto payload = unframe(kTargetSelectionDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto registryRevision = decoder.decodeDigest();
  auto profileBytes = decoder.decodeByteString(kMaximumProfileBytes);
  auto semanticProjection = identity::CanonicalTargetSpecificationKey::decodeCanonical(decoder);
  auto panicStrategy = decoder.decodeUint8();
  if (registryRevision == zc::none || profileBytes == zc::none || semanticProjection == zc::none ||
      panicStrategy == zc::none || !decoder.finished() ||
      zero(ZC_ASSERT_NONNULL(registryRevision)) ||
      !valid(static_cast<PackagePanicStrategy>(ZC_ASSERT_NONNULL(panicStrategy)))) {
    return zc::none;
  }
  auto profileText = zc::str(ZC_ASSERT_NONNULL(profileBytes).asChars());
  auto profile = RegisteredTargetProfileName::from(profileText);
  if (profile == zc::none) { return zc::none; }
  CanonicalTargetSelectionRecord result(
      zc::heap<Impl>(ZC_ASSERT_NONNULL(registryRevision), zc::mv(ZC_ASSERT_NONNULL(profile)),
                     zc::mv(ZC_ASSERT_NONNULL(semanticProjection)),
                     static_cast<PackagePanicStrategy>(ZC_ASSERT_NONNULL(panicStrategy))));
  if (result.encodeCanonical().asPtr() != bytes) { return zc::none; }
  return result;
}

CanonicalTargetSelectionRecord CanonicalTargetSelectionRecord::clone() const {
  return CanonicalTargetSelectionRecord(
      zc::heap<Impl>(impl->registryRevision, impl->profile.clone(),
                     impl->semanticProjection.clone(), impl->panicStrategy));
}
const identity::Sha256Digest& CanonicalTargetSelectionRecord::registryRevision() const noexcept {
  return impl->registryRevision;
}
zc::StringPtr CanonicalTargetSelectionRecord::profile() const noexcept {
  return impl->profile.text();
}
const identity::CanonicalTargetSpecificationKey&
CanonicalTargetSelectionRecord::semanticProjection() const noexcept {
  return impl->semanticProjection;
}
PackagePanicStrategy CanonicalTargetSelectionRecord::panicStrategy() const noexcept {
  return impl->panicStrategy;
}
zc::Array<uint8_t> CanonicalTargetSelectionRecord::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(impl->registryRevision);
  impl->profile.encode(encoder);
  impl->semanticProjection.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(impl->panicStrategy));
  auto payload = encoder.finish();
  size_t encodedSize;
  ZC_REQUIRE(beginRecordSize(kTargetSelectionDomain, encodedSize) &&
                 addSize(encodedSize, payload.size()) && encodedSize <= UINT32_MAX,
             "canonical target selection must fit RFC byte bound");
  return frame(kTargetSelectionDomain, payload.asPtr());
}
bool CanonicalTargetSelectionRecord::operator==(const CanonicalTargetSelectionRecord& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

struct CanonicalLanguageOptionsRecord::Impl final {
  bool useUnicode;
  bool allowDollarIdentifiers;
  bool supportRegexLiterals;
};

CanonicalLanguageOptionsRecord::CanonicalLanguageOptionsRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalLanguageOptionsRecord::~CanonicalLanguageOptionsRecord() noexcept(false) = default;
CanonicalLanguageOptionsRecord::CanonicalLanguageOptionsRecord(
    CanonicalLanguageOptionsRecord&&) noexcept = default;
CanonicalLanguageOptionsRecord& CanonicalLanguageOptionsRecord::operator=(
    CanonicalLanguageOptionsRecord&&) noexcept = default;

CanonicalLanguageOptionsRecord CanonicalLanguageOptionsRecord::project(
    const SelectedLanguageOptions& options) {
  return CanonicalLanguageOptionsRecord(zc::heap<Impl>(
      Impl{options.useUnicode, options.allowDollarIdentifiers, options.supportRegexLiterals}));
}
zc::Maybe<CanonicalLanguageOptionsRecord> CanonicalLanguageOptionsRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > UINT32_MAX) { return zc::none; }
  auto payload = unframe(kLanguageOptionsDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto useUnicode = decoder.decodeBool();
  auto allowDollarIdentifiers = decoder.decodeBool();
  auto supportRegexLiterals = decoder.decodeBool();
  if (useUnicode == zc::none || allowDollarIdentifiers == zc::none ||
      supportRegexLiterals == zc::none || !decoder.finished()) {
    return zc::none;
  }
  return CanonicalLanguageOptionsRecord(
      zc::heap<Impl>(Impl{ZC_ASSERT_NONNULL(useUnicode), ZC_ASSERT_NONNULL(allowDollarIdentifiers),
                          ZC_ASSERT_NONNULL(supportRegexLiterals)}));
}
CanonicalLanguageOptionsRecord CanonicalLanguageOptionsRecord::clone() const {
  return CanonicalLanguageOptionsRecord(zc::heap<Impl>(
      Impl{impl->useUnicode, impl->allowDollarIdentifiers, impl->supportRegexLiterals}));
}
bool CanonicalLanguageOptionsRecord::useUnicode() const noexcept { return impl->useUnicode; }
bool CanonicalLanguageOptionsRecord::allowDollarIdentifiers() const noexcept {
  return impl->allowDollarIdentifiers;
}
bool CanonicalLanguageOptionsRecord::supportRegexLiterals() const noexcept {
  return impl->supportRegexLiterals;
}
zc::Array<uint8_t> CanonicalLanguageOptionsRecord::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeBool(impl->useUnicode);
  encoder.encodeBool(impl->allowDollarIdentifiers);
  encoder.encodeBool(impl->supportRegexLiterals);
  return frame(kLanguageOptionsDomain, encoder.finish().asPtr());
}
bool CanonicalLanguageOptionsRecord::operator==(
    const CanonicalLanguageOptionsRecord& other) const noexcept {
  return impl->useUnicode == other.impl->useUnicode &&
         impl->allowDollarIdentifiers == other.impl->allowDollarIdentifiers &&
         impl->supportRegexLiterals == other.impl->supportRegexLiterals;
}

struct CanonicalPackageCompilationRequest::Impl final {
  Impl(zc::Vector<CanonicalCompilationRootRecord>&& roots,
       CanonicalTargetSelectionRecord&& hostTarget, CanonicalTargetSelectionRecord&& target,
       CanonicalLanguageOptionsRecord&& languageOptions, PackageLockMode lockMode) noexcept
      : roots(zc::mv(roots)),
        hostTarget(zc::mv(hostTarget)),
        target(zc::mv(target)),
        languageOptions(zc::mv(languageOptions)),
        lockMode(lockMode) {}
  zc::Vector<CanonicalCompilationRootRecord> roots;
  CanonicalTargetSelectionRecord hostTarget;
  CanonicalTargetSelectionRecord target;
  CanonicalLanguageOptionsRecord languageOptions;
  PackageLockMode lockMode;
};

CanonicalPackageCompilationRequest::CanonicalPackageCompilationRequest(
    zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalPackageCompilationRequest::~CanonicalPackageCompilationRequest() noexcept(false) = default;
CanonicalPackageCompilationRequest::CanonicalPackageCompilationRequest(
    CanonicalPackageCompilationRequest&&) noexcept = default;
CanonicalPackageCompilationRequest& CanonicalPackageCompilationRequest::operator=(
    CanonicalPackageCompilationRequest&&) noexcept = default;

zc::Maybe<CanonicalPackageCompilationRequest> CanonicalPackageCompilationRequest::fromVerified(
    const VerifiedPackageCompilationRequest& request) {
  if (request.roots().size() == 0 || request.roots().size() > UINT32_MAX ||
      request.roots().size() > SIZE_MAX / sizeof(CanonicalCompilationRootRecord) ||
      !valid(request.lockMode())) {
    return zc::none;
  }
  size_t encodedSize;
  if (!beginRecordSize(kPackageRequestDomain, encodedSize) ||
      !addSize(encodedSize, kEncodedCountBytes) || encodedSize > UINT32_MAX) {
    return zc::none;
  }
  zc::Array<uint8_t> previous;
  for (const auto& root : request.roots()) {
    auto encoded = encodeLiveCompilationRootProjection(root);
    if (encoded == zc::none ||
        (previous.size() != 0 && !(previous.asPtr() < ZC_ASSERT_NONNULL(encoded).asPtr())) ||
        !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(encoded).size()) ||
        encodedSize > UINT32_MAX) {
      return zc::none;
    }
    previous = zc::mv(ZC_ASSERT_NONNULL(encoded));
  }
  if (request.hostTarget().registryRevision() != request.target().registryRevision()) {
    return zc::none;
  }
  auto hostBytes = encodeLiveTargetSelectionProjection(request.hostTarget());
  if (hostBytes == zc::none || !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(hostBytes).size()) ||
      encodedSize > UINT32_MAX) {
    return zc::none;
  }
  auto targetBytes = encodeLiveTargetSelectionProjection(request.target());
  if (targetBytes == zc::none ||
      !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(targetBytes).size()) ||
      encodedSize > UINT32_MAX) {
    return zc::none;
  }
  auto languageBytes = encodeLiveLanguageOptionsProjection(request.languageOptions());
  if (!addFramedSize(encodedSize, languageBytes.size()) || !addSize(encodedSize, sizeof(uint8_t)) ||
      encodedSize > UINT32_MAX) {
    return zc::none;
  }

  zc::Vector<CanonicalCompilationRootRecord> roots(request.roots().size());
  for (const auto& root : request.roots()) {
    auto projected = CanonicalCompilationRootRecord::project(root);
    if (projected == zc::none) { return zc::none; }
    roots.add(zc::mv(ZC_ASSERT_NONNULL(projected)));
  }
  auto hostTarget = CanonicalTargetSelectionRecord::project(request.hostTarget());
  auto target = CanonicalTargetSelectionRecord::project(request.target());
  if (hostTarget == zc::none || target == zc::none) { return zc::none; }
  auto languageOptions = CanonicalLanguageOptionsRecord::project(request.languageOptions());
  return CanonicalPackageCompilationRequest(zc::heap<Impl>(
      zc::mv(roots), zc::mv(ZC_ASSERT_NONNULL(hostTarget)), zc::mv(ZC_ASSERT_NONNULL(target)),
      zc::mv(languageOptions), request.lockMode()));
}

zc::Maybe<CanonicalPackageCompilationRequest> CanonicalPackageCompilationRequest::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > UINT32_MAX) { return zc::none; }
  auto payload = unframe(kPackageRequestDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto rootCount = decoder.decodeSequenceSize(kMaximumSchemaCount);
  if (rootCount == zc::none || ZC_ASSERT_NONNULL(rootCount) == 0 ||
      ZC_ASSERT_NONNULL(rootCount) > static_cast<uint64_t>(SIZE_MAX) ||
      ZC_ASSERT_NONNULL(rootCount) > decoder.remaining() / kEncodedCountBytes ||
      ZC_ASSERT_NONNULL(rootCount) > SIZE_MAX / sizeof(CanonicalCompilationRootRecord)) {
    return zc::none;
  }
  size_t encodedSize;
  if (!beginRecordSize(kPackageRequestDomain, encodedSize) ||
      !addSize(encodedSize, kEncodedCountBytes) || encodedSize > UINT32_MAX) {
    return zc::none;
  }
  zc::Vector<CanonicalCompilationRootRecord> roots(
      static_cast<size_t>(ZC_ASSERT_NONNULL(rootCount)));
  zc::Array<uint8_t> previous;
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(rootCount); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumSchemaCount);
    if (encoded == zc::none ||
        (previous.size() != 0 && !(previous.asPtr() < ZC_ASSERT_NONNULL(encoded).asPtr())) ||
        !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(encoded).size()) ||
        encodedSize > UINT32_MAX) {
      return zc::none;
    }
    auto root = CanonicalCompilationRootRecord::decodeCanonical(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (root == zc::none) { return zc::none; }
    previous = zc::mv(ZC_ASSERT_NONNULL(encoded));
    roots.add(zc::mv(ZC_ASSERT_NONNULL(root)));
  }
  auto hostBytes = decoder.decodeByteString(kMaximumSchemaCount);
  auto targetBytes = decoder.decodeByteString(kMaximumSchemaCount);
  auto languageBytes = decoder.decodeByteString(kMaximumSchemaCount);
  auto lockMode = decoder.decodeUint8();
  if (hostBytes == zc::none || targetBytes == zc::none || languageBytes == zc::none ||
      lockMode == zc::none || !decoder.finished() ||
      !valid(static_cast<PackageLockMode>(ZC_ASSERT_NONNULL(lockMode))) ||
      !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(hostBytes).size()) ||
      !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(targetBytes).size()) ||
      !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(languageBytes).size()) ||
      !addSize(encodedSize, sizeof(uint8_t)) || encodedSize > UINT32_MAX ||
      encodedSize != bytes.size()) {
    return zc::none;
  }
  auto hostTarget =
      CanonicalTargetSelectionRecord::decodeCanonical(ZC_ASSERT_NONNULL(hostBytes).asPtr());
  auto target =
      CanonicalTargetSelectionRecord::decodeCanonical(ZC_ASSERT_NONNULL(targetBytes).asPtr());
  auto languageOptions =
      CanonicalLanguageOptionsRecord::decodeCanonical(ZC_ASSERT_NONNULL(languageBytes).asPtr());
  if (hostTarget == zc::none || target == zc::none || languageOptions == zc::none ||
      ZC_ASSERT_NONNULL(hostTarget).registryRevision() !=
          ZC_ASSERT_NONNULL(target).registryRevision()) {
    return zc::none;
  }
  return CanonicalPackageCompilationRequest(
      zc::heap<Impl>(zc::mv(roots), zc::mv(ZC_ASSERT_NONNULL(hostTarget)),
                     zc::mv(ZC_ASSERT_NONNULL(target)), zc::mv(ZC_ASSERT_NONNULL(languageOptions)),
                     static_cast<PackageLockMode>(ZC_ASSERT_NONNULL(lockMode))));
}

CanonicalPackageCompilationRequest CanonicalPackageCompilationRequest::clone() const {
  zc::Vector<CanonicalCompilationRootRecord> roots(impl->roots.size());
  for (const auto& root : impl->roots) { roots.add(root.clone()); }
  return CanonicalPackageCompilationRequest(
      zc::heap<Impl>(zc::mv(roots), impl->hostTarget.clone(), impl->target.clone(),
                     impl->languageOptions.clone(), impl->lockMode));
}
zc::ArrayPtr<const CanonicalCompilationRootRecord> CanonicalPackageCompilationRequest::roots()
    const noexcept {
  return impl->roots.asPtr();
}
const CanonicalTargetSelectionRecord& CanonicalPackageCompilationRequest::hostTarget()
    const noexcept {
  return impl->hostTarget;
}
const CanonicalTargetSelectionRecord& CanonicalPackageCompilationRequest::target() const noexcept {
  return impl->target;
}
const CanonicalLanguageOptionsRecord& CanonicalPackageCompilationRequest::languageOptions()
    const noexcept {
  return impl->languageOptions;
}
PackageLockMode CanonicalPackageCompilationRequest::lockMode() const noexcept {
  return impl->lockMode;
}
zc::Array<uint8_t> CanonicalPackageCompilationRequest::encodeCanonical() const {
  size_t encodedSize;
  ZC_REQUIRE(beginRecordSize(kPackageRequestDomain, encodedSize) &&
                 addSize(encodedSize, kEncodedCountBytes) && encodedSize <= UINT32_MAX,
             "canonical package request must fit RFC byte bound");
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(impl->roots.size());
  for (const auto& root : impl->roots) {
    auto encoded = root.encodeCanonical();
    ZC_REQUIRE(addFramedSize(encodedSize, encoded.size()) && encodedSize <= UINT32_MAX,
               "canonical package request roots must fit RFC byte bound");
    encoder.encodeByteString(encoded.asPtr());
  }
  auto hostTarget = impl->hostTarget.encodeCanonical();
  ZC_REQUIRE(addFramedSize(encodedSize, hostTarget.size()) && encodedSize <= UINT32_MAX,
             "canonical package request host target must fit RFC byte bound");
  encoder.encodeByteString(hostTarget.asPtr());
  auto target = impl->target.encodeCanonical();
  ZC_REQUIRE(addFramedSize(encodedSize, target.size()) && encodedSize <= UINT32_MAX,
             "canonical package request target must fit RFC byte bound");
  encoder.encodeByteString(target.asPtr());
  auto languageOptions = impl->languageOptions.encodeCanonical();
  ZC_REQUIRE(addFramedSize(encodedSize, languageOptions.size()) &&
                 addSize(encodedSize, sizeof(uint8_t)) && encodedSize <= UINT32_MAX,
             "canonical package request fields must fit RFC byte bound");
  encoder.encodeByteString(languageOptions.asPtr());
  encoder.encodeUint8(static_cast<uint8_t>(impl->lockMode));
  auto encoded = frame(kPackageRequestDomain, encoder.finish().asPtr());
  ZC_REQUIRE(encoded.size() == encodedSize, "canonical package request size must match encoding");
  return encoded;
}
bool CanonicalPackageCompilationRequest::operator==(
    const CanonicalPackageCompilationRequest& other) const {
  if (impl->roots.size() != other.impl->roots.size() ||
      !(impl->hostTarget == other.impl->hostTarget) || !(impl->target == other.impl->target) ||
      !(impl->languageOptions == other.impl->languageOptions) ||
      impl->lockMode != other.impl->lockMode) {
    return false;
  }
  for (size_t index = 0; index < impl->roots.size(); ++index) {
    if (!(impl->roots[index] == other.impl->roots[index])) { return false; }
  }
  return true;
}

namespace {

zc::Maybe<zc::Array<uint8_t>> encodeIndependentRootProjection(const VerifiedCompilationRoot& root) {
  if (!valid(root.targetKind()) || root.sourcePath().segments().size() > UINT32_MAX ||
      identity::TargetName::fromCanonical(root.targetName()) == zc::none) {
    return zc::none;
  }
  auto package = root.packageKey().encode();
  if (package.size() > UINT32_MAX) { return zc::none; }
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(package.asPtr());
  encoder.encodeUint8(static_cast<uint8_t>(root.targetKind()));
  encoder.encodeByteString(root.targetName().asBytes());
  encoder.encodeUint32(root.editionYear());
  encoder.encodeBool(root.requiresBuildScript());
  root.sourcePath().encode(encoder);
  auto payload = encoder.finish();
  size_t encodedSize;
  if (!beginRecordSize(kCompilationRootDomain, encodedSize) ||
      !addSize(encodedSize, payload.size()) || encodedSize > UINT32_MAX) {
    return zc::none;
  }
  return frame(kCompilationRootDomain, payload.asPtr());
}

zc::Maybe<zc::Array<uint8_t>> encodeIndependentTargetProjection(
    const RegisteredTargetSelection& selection) {
  if (zero(selection.registryRevision()) || !valid(selection.panicStrategy()) ||
      RegisteredTargetProfileName::from(selection.profile()) == zc::none) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(selection.registryRevision());
  encoder.encodeByteString(selection.profile().asBytes());
  selection.semanticProjection().encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(selection.panicStrategy()));
  auto payload = encoder.finish();
  size_t encodedSize;
  if (!beginRecordSize(kTargetSelectionDomain, encodedSize) ||
      !addSize(encodedSize, payload.size()) || encodedSize > UINT32_MAX) {
    return zc::none;
  }
  return frame(kTargetSelectionDomain, payload.asPtr());
}

zc::Array<uint8_t> encodeIndependentLanguageProjection(const SelectedLanguageOptions& options) {
  identity::CanonicalEncoder encoder;
  encoder.encodeBool(options.useUnicode);
  encoder.encodeBool(options.allowDollarIdentifiers);
  encoder.encodeBool(options.supportRegexLiterals);
  return frame(kLanguageOptionsDomain, encoder.finish().asPtr());
}

}  // namespace

bool CanonicalPackageCompilationRequestProjectionVerifier::verify(
    const CanonicalPackageCompilationRequest& candidate,
    const VerifiedPackageCompilationRequest& request) {
  if (request.roots().size() == 0 || request.roots().size() > UINT32_MAX ||
      !valid(request.lockMode()) || zero(request.hostTarget().registryRevision()) ||
      request.hostTarget().registryRevision() != request.target().registryRevision()) {
    return false;
  }
  size_t encodedSize;
  if (!beginRecordSize(kPackageRequestDomain, encodedSize) ||
      !addSize(encodedSize, kEncodedCountBytes) || encodedSize > UINT32_MAX) {
    return false;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(request.roots().size());
  zc::Array<uint8_t> previous;
  for (const auto& root : request.roots()) {
    auto encoded = encodeIndependentRootProjection(root);
    if (encoded == zc::none ||
        (previous.size() != 0 && !(previous.asPtr() < ZC_ASSERT_NONNULL(encoded).asPtr())) ||
        !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(encoded).size()) ||
        encodedSize > UINT32_MAX) {
      return false;
    }
    encoder.encodeByteString(ZC_ASSERT_NONNULL(encoded).asPtr());
    previous = zc::mv(ZC_ASSERT_NONNULL(encoded));
  }
  auto hostTarget = encodeIndependentTargetProjection(request.hostTarget());
  if (hostTarget == zc::none || !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(hostTarget).size()) ||
      encodedSize > UINT32_MAX) {
    return false;
  }
  encoder.encodeByteString(ZC_ASSERT_NONNULL(hostTarget).asPtr());
  auto target = encodeIndependentTargetProjection(request.target());
  if (target == zc::none || !addFramedSize(encodedSize, ZC_ASSERT_NONNULL(target).size()) ||
      encodedSize > UINT32_MAX) {
    return false;
  }
  encoder.encodeByteString(ZC_ASSERT_NONNULL(target).asPtr());
  auto languageOptions = encodeIndependentLanguageProjection(request.languageOptions());
  if (!addFramedSize(encodedSize, languageOptions.size()) ||
      !addSize(encodedSize, sizeof(uint8_t)) || encodedSize > UINT32_MAX) {
    return false;
  }
  encoder.encodeByteString(languageOptions.asPtr());
  encoder.encodeUint8(static_cast<uint8_t>(request.lockMode()));
  const auto expected = frame(kPackageRequestDomain, encoder.finish().asPtr());
  const auto actual = candidate.encodeCanonical();
  if (expected.size() != encodedSize || actual.asPtr() != expected.asPtr()) { return false; }
  auto decoded = CanonicalPackageCompilationRequest::decodeCanonical(actual.asPtr());
  return decoded != zc::none && ZC_ASSERT_NONNULL(decoded) == candidate;
}

}  // namespace zomlang::compiler::driver::package
