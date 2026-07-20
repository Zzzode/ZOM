// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/identity-pre-admission.h"

#include "zc/core/debug.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {

namespace identity_pre_admission_detail {

struct IdentitySyntaxSiteKeyData final {
  identity::ModuleKey module;
  identity::SourceFileKey source;
  zc::Vector<uint32_t> moduleSyntaxPath;
};

struct IdentitySyntaxSiteData final {
  IdentitySyntaxSiteKey key;
  identity::SourceSpan range;
};

struct DuplicateBoundOccurrenceData final {
  identity::CanonicalBoundObligation obligation;
  IdentitySyntaxSiteKey first;
  IdentitySyntaxSiteKey duplicate;
};

struct DefinitionCandidateIdentity final {
  identity::DefinitionIdentityRecord record;
};

struct ImplCandidateIdentity final {
  identity::ImplIdentityRecord record;
};

using CandidateIdentity = zc::OneOf<DefinitionCandidateIdentity, ImplCandidateIdentity>;

struct PreAdmissionIdentityCandidateData final {
  CandidateIdentity identity;
  zc::Maybe<identity::OverloadHeaderAuthority> overloadHeader;
  IdentitySyntaxSiteKey site;
  zc::Vector<DuplicateBoundOccurrence> duplicateBounds;
};

struct ImplSourceOccurrenceKeyData final {
  identity::ImplKey implementation;
  IdentitySyntaxSiteKey site;
};

struct ImplIdentityOccurrenceGroupData final {
  identity::ImplKey implementation;
  identity::ImplId authority;
  zc::Vector<ImplSourceOccurrenceKey> occurrences;
};

}  // namespace identity_pre_admission_detail

namespace detail = identity_pre_admission_detail;
namespace {

constexpr uint64_t kMaximumIdentitySyntaxPathComponents = 4096;

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return leftBytes.asPtr() == rightBytes.asPtr();
}

int comparePath(zc::ArrayPtr<const uint32_t> left, zc::ArrayPtr<const uint32_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool sameSiteModuleAndSource(const IdentitySyntaxSiteKey& left,
                             const IdentitySyntaxSiteKey& right) {
  return sameModule(left.module(), right.module()) && left.source().sameAs(right.source());
}

bool sameObligation(const identity::CanonicalBoundObligation& left,
                    const identity::CanonicalBoundObligation& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return leftBytes.asPtr() == rightBytes.asPtr();
}

bool containsObligation(zc::ArrayPtr<const identity::CanonicalBoundObligation> obligations,
                        const identity::CanonicalBoundObligation& expected) {
  for (const auto& obligation : obligations) {
    if (sameObligation(obligation, expected)) { return true; }
  }
  return false;
}

zc::Maybe<identity::OverloadHeaderAuthority> cloneOverloadHeader(
    const zc::Maybe<identity::OverloadHeaderAuthority>& authority) {
  ZC_IF_SOME(value, authority) { return value.clone(); }
  return zc::none;
}

void encodeOverloadHeaderAuthority(identity::CanonicalEncoder& encoder,
                                   const identity::OverloadHeaderAuthority& authority) {
  authority.digest().encode(encoder);
  authority.header().encode(encoder);
}

bool validateDuplicateBounds(zc::ArrayPtr<const DuplicateBoundOccurrence> duplicateBounds,
                             const IdentitySyntaxSiteKey& candidateSite,
                             zc::ArrayPtr<const identity::CanonicalBoundObligation> obligations) {
  const IdentitySyntaxSiteKey* previousDuplicate = nullptr;
  for (const auto& duplicate : duplicateBounds) {
    if (!sameSiteModuleAndSource(candidateSite, duplicate.first()) ||
        !sameSiteModuleAndSource(candidateSite, duplicate.duplicate()) ||
        !containsObligation(obligations, duplicate.obligation())) {
      return false;
    }
    if (previousDuplicate != nullptr) {
      if (!previousDuplicate->source().sameAs(duplicate.duplicate().source())) {
        const auto previousSource = previousDuplicate->source().encode();
        const auto currentSource = duplicate.duplicate().source().encode();
        if (!lessBytes(previousSource.asPtr(), currentSource.asPtr())) { return false; }
      } else if (comparePath(previousDuplicate->moduleSyntaxPath(),
                             duplicate.duplicate().moduleSyntaxPath()) >= 0) {
        return false;
      }
    }
    previousDuplicate = &duplicate.duplicate();
  }
  return true;
}

zc::Maybe<const IdentitySyntaxSite&> resolveSite(const IdentitySyntaxSiteKey& key,
                                                 zc::ArrayPtr<const IdentitySyntaxSite> sites) {
  const IdentitySyntaxSite* result = nullptr;
  for (const auto& site : sites) {
    if (!site.key().sameAs(key)) { continue; }
    if (result != nullptr) { return zc::none; }
    result = &site;
  }
  if (result == nullptr) { return zc::none; }
  return *result;
}

}  // namespace

IdentitySyntaxSiteKey::IdentitySyntaxSiteKey(
    zc::Own<detail::IdentitySyntaxSiteKeyData>&& value) noexcept
    : impl(zc::mv(value)) {}
IdentitySyntaxSiteKey::~IdentitySyntaxSiteKey() noexcept(false) = default;
IdentitySyntaxSiteKey::IdentitySyntaxSiteKey(IdentitySyntaxSiteKey&&) noexcept = default;
IdentitySyntaxSiteKey& IdentitySyntaxSiteKey::operator=(IdentitySyntaxSiteKey&&) noexcept = default;

zc::Maybe<IdentitySyntaxSiteKey> IdentitySyntaxSiteKey::from(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    zc::Vector<uint32_t>&& moduleSyntaxPath) {
  if (!source.belongsTo(module.crate()) ||
      moduleSyntaxPath.size() > kMaximumIdentitySyntaxPathComponents) {
    return zc::none;
  }
  return IdentitySyntaxSiteKey(zc::heap<detail::IdentitySyntaxSiteKeyData>(
      detail::IdentitySyntaxSiteKeyData{zc::mv(module), zc::mv(source), zc::mv(moduleSyntaxPath)}));
}

zc::Maybe<IdentitySyntaxSiteKey> IdentitySyntaxSiteKey::decodeCanonical(
    identity::CanonicalDecoder& decoder) {
  auto module = identity::ModuleKey::decodeCanonical(decoder);
  if (module == zc::none) { return zc::none; }
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none) { return zc::none; }
  auto count = decoder.decodeSequenceSize(kMaximumIdentitySyntaxPathComponents);
  if (count == zc::none) { return zc::none; }
  zc::Vector<uint32_t> path(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  ZC_IF_SOME(value, count) {
    for (uint64_t index = 0; index < value; ++index) {
      auto component = decoder.decodeUint32();
      if (component == zc::none) { return zc::none; }
      ZC_IF_SOME(componentValue, component) { path.add(componentValue); }
    }
  }
  ZC_IF_SOME(moduleValue, module) {
    ZC_IF_SOME(sourceValue, source) {
      return from(zc::mv(moduleValue), zc::mv(sourceValue), zc::mv(path));
    }
  }
  return zc::none;
}

IdentitySyntaxSiteKey IdentitySyntaxSiteKey::clone() const {
  zc::Vector<uint32_t> path(impl->moduleSyntaxPath.size());
  path.addAll(impl->moduleSyntaxPath);
  auto result = from(impl->module.clone(), impl->source.clone(), zc::mv(path));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

const identity::ModuleKey& IdentitySyntaxSiteKey::module() const noexcept { return impl->module; }
const identity::SourceFileKey& IdentitySyntaxSiteKey::source() const noexcept {
  return impl->source;
}
zc::ArrayPtr<const uint32_t> IdentitySyntaxSiteKey::moduleSyntaxPath() const noexcept {
  return impl->moduleSyntaxPath.asPtr();
}
bool IdentitySyntaxSiteKey::sameAs(const IdentitySyntaxSiteKey& other) const {
  return sameModule(impl->module, other.impl->module) && impl->source.sameAs(other.impl->source) &&
         comparePath(impl->moduleSyntaxPath.asPtr(), other.impl->moduleSyntaxPath.asPtr()) == 0;
}
void IdentitySyntaxSiteKey::encode(identity::CanonicalEncoder& encoder) const {
  impl->module.encode(encoder);
  impl->source.encode(encoder);
  encoder.encodeSequenceSize(impl->moduleSyntaxPath.size());
  for (const auto component : impl->moduleSyntaxPath) { encoder.encodeUint32(component); }
}
zc::Array<uint8_t> IdentitySyntaxSiteKey::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

IdentitySyntaxSite::IdentitySyntaxSite(zc::Own<detail::IdentitySyntaxSiteData>&& value) noexcept
    : impl(zc::mv(value)) {}
IdentitySyntaxSite::~IdentitySyntaxSite() noexcept(false) = default;
IdentitySyntaxSite::IdentitySyntaxSite(IdentitySyntaxSite&&) noexcept = default;
IdentitySyntaxSite& IdentitySyntaxSite::operator=(IdentitySyntaxSite&&) noexcept = default;

zc::Maybe<IdentitySyntaxSite> IdentitySyntaxSite::from(IdentitySyntaxSiteKey&& key,
                                                       identity::SourceSpan&& range) {
  if (!range.belongsTo(key.source())) { return zc::none; }
  return IdentitySyntaxSite(zc::heap<detail::IdentitySyntaxSiteData>(
      detail::IdentitySyntaxSiteData{zc::mv(key), zc::mv(range)}));
}
IdentitySyntaxSite IdentitySyntaxSite::clone() const {
  auto result = from(impl->key.clone(), impl->range.clone());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}
const IdentitySyntaxSiteKey& IdentitySyntaxSite::key() const noexcept { return impl->key; }
const identity::SourceSpan& IdentitySyntaxSite::range() const noexcept { return impl->range; }
bool IdentitySyntaxSite::sourceOrderLessThan(const IdentitySyntaxSite& other) const {
  if (!impl->key.source().sameAs(other.impl->key.source())) {
    const auto left = impl->key.source().encode();
    const auto right = other.impl->key.source().encode();
    return lessBytes(left.asPtr(), right.asPtr());
  }
  if (impl->range.byteStart() != other.impl->range.byteStart()) {
    return impl->range.byteStart() < other.impl->range.byteStart();
  }
  if (impl->range.byteEnd() != other.impl->range.byteEnd()) {
    return impl->range.byteEnd() < other.impl->range.byteEnd();
  }
  return comparePath(impl->key.moduleSyntaxPath(), other.impl->key.moduleSyntaxPath()) < 0;
}
void IdentitySyntaxSite::encode(identity::CanonicalEncoder& encoder) const {
  impl->key.encode(encoder);
  encoder.encodeUint64(impl->range.byteStart());
  encoder.encodeUint64(impl->range.byteEnd());
}
zc::Array<uint8_t> IdentitySyntaxSite::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

DuplicateBoundOccurrence::DuplicateBoundOccurrence(
    zc::Own<detail::DuplicateBoundOccurrenceData>&& value) noexcept
    : impl(zc::mv(value)) {}
DuplicateBoundOccurrence::~DuplicateBoundOccurrence() noexcept(false) = default;
DuplicateBoundOccurrence::DuplicateBoundOccurrence(DuplicateBoundOccurrence&&) noexcept = default;
DuplicateBoundOccurrence& DuplicateBoundOccurrence::operator=(DuplicateBoundOccurrence&&) noexcept =
    default;

zc::Maybe<DuplicateBoundOccurrence> DuplicateBoundOccurrence::from(
    identity::CanonicalBoundObligation&& obligation, IdentitySyntaxSiteKey&& first,
    IdentitySyntaxSiteKey&& duplicate) {
  if (!sameSiteModuleAndSource(first, duplicate) || first.sameAs(duplicate) ||
      comparePath(first.moduleSyntaxPath(), duplicate.moduleSyntaxPath()) >= 0) {
    return zc::none;
  }
  return DuplicateBoundOccurrence(zc::heap<detail::DuplicateBoundOccurrenceData>(
      detail::DuplicateBoundOccurrenceData{zc::mv(obligation), zc::mv(first), zc::mv(duplicate)}));
}
DuplicateBoundOccurrence DuplicateBoundOccurrence::clone() const {
  auto result = from(impl->obligation.clone(), impl->first.clone(), impl->duplicate.clone());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}
const identity::CanonicalBoundObligation& DuplicateBoundOccurrence::obligation() const noexcept {
  return impl->obligation;
}
const IdentitySyntaxSiteKey& DuplicateBoundOccurrence::first() const noexcept {
  return impl->first;
}
const IdentitySyntaxSiteKey& DuplicateBoundOccurrence::duplicate() const noexcept {
  return impl->duplicate;
}
void DuplicateBoundOccurrence::encode(identity::CanonicalEncoder& encoder) const {
  impl->obligation.encode(encoder);
  impl->first.encode(encoder);
  impl->duplicate.encode(encoder);
}
zc::Array<uint8_t> DuplicateBoundOccurrence::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

PreAdmissionIdentityCandidate::PreAdmissionIdentityCandidate(
    zc::Own<detail::PreAdmissionIdentityCandidateData>&& value) noexcept
    : impl(zc::mv(value)) {}
PreAdmissionIdentityCandidate::~PreAdmissionIdentityCandidate() noexcept(false) = default;
PreAdmissionIdentityCandidate::PreAdmissionIdentityCandidate(
    PreAdmissionIdentityCandidate&&) noexcept = default;
PreAdmissionIdentityCandidate& PreAdmissionIdentityCandidate::operator=(
    PreAdmissionIdentityCandidate&&) noexcept = default;

zc::Maybe<PreAdmissionIdentityCandidate> PreAdmissionIdentityCandidate::definition(
    identity::DefinitionIdentityRecord&& record,
    zc::Maybe<identity::OverloadHeaderAuthority>&& overloadHeader, IdentitySyntaxSiteKey&& site,
    zc::Vector<DuplicateBoundOccurrence>&& duplicateBounds) {
  if (!sameModule(record.module(), site.module())) { return zc::none; }
  auto checkedAuthority = identity::DefinitionIdentityAuthority::from(
      record.clone(), cloneOverloadHeader(overloadHeader));
  if (checkedAuthority == zc::none) { return zc::none; }

  zc::ArrayPtr<const identity::CanonicalBoundObligation> obligations;
  ZC_IF_SOME(authority, overloadHeader) { obligations = authority.header().obligations(); }
  if (!validateDuplicateBounds(duplicateBounds.asPtr(), site, obligations)) { return zc::none; }

  detail::CandidateIdentity identityValue(detail::DefinitionCandidateIdentity{zc::mv(record)});
  return PreAdmissionIdentityCandidate(
      zc::heap<detail::PreAdmissionIdentityCandidateData>(detail::PreAdmissionIdentityCandidateData{
          zc::mv(identityValue), zc::mv(overloadHeader), zc::mv(site), zc::mv(duplicateBounds)}));
}

zc::Maybe<PreAdmissionIdentityCandidate> PreAdmissionIdentityCandidate::implementation(
    identity::ImplIdentityRecord&& record,
    zc::Maybe<identity::OverloadHeaderAuthority>&& overloadHeader, IdentitySyntaxSiteKey&& site,
    zc::Vector<DuplicateBoundOccurrence>&& duplicateBounds) {
  if (overloadHeader != zc::none || !sameModule(record.module(), site.module()) ||
      !validateDuplicateBounds(duplicateBounds.asPtr(), site, record.header().obligations())) {
    return zc::none;
  }
  auto authority = identity::ImplIdentityAuthority::from(record.clone());
  if (!authority.verify()) { return zc::none; }

  detail::CandidateIdentity identityValue(detail::ImplCandidateIdentity{zc::mv(record)});
  return PreAdmissionIdentityCandidate(
      zc::heap<detail::PreAdmissionIdentityCandidateData>(detail::PreAdmissionIdentityCandidateData{
          zc::mv(identityValue), zc::mv(overloadHeader), zc::mv(site), zc::mv(duplicateBounds)}));
}

PreAdmissionIdentityCandidate PreAdmissionIdentityCandidate::clone() const {
  zc::Vector<DuplicateBoundOccurrence> duplicateBounds(impl->duplicateBounds.size());
  for (const auto& duplicate : impl->duplicateBounds) { duplicateBounds.add(duplicate.clone()); }
  auto overloadHeader = cloneOverloadHeader(impl->overloadHeader);
  if (impl->identity.is<detail::DefinitionCandidateIdentity>()) {
    auto result =
        definition(impl->identity.get<detail::DefinitionCandidateIdentity>().record.clone(),
                   zc::mv(overloadHeader), impl->site.clone(), zc::mv(duplicateBounds));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  } else {
    auto result =
        implementation(impl->identity.get<detail::ImplCandidateIdentity>().record.clone(),
                       zc::mv(overloadHeader), impl->site.clone(), zc::mv(duplicateBounds));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_UNREACHABLE
}
PreAdmissionIdentityKind PreAdmissionIdentityCandidate::kind() const noexcept {
  return impl->identity.is<detail::DefinitionCandidateIdentity>()
             ? PreAdmissionIdentityKind::Definition
             : PreAdmissionIdentityKind::Implementation;
}
zc::Maybe<const identity::DefinitionIdentityRecord&>
PreAdmissionIdentityCandidate::definitionRecord() const noexcept {
  ZC_IF_SOME(value, impl->identity.tryGet<detail::DefinitionCandidateIdentity>()) {
    return value.record;
  }
  return zc::none;
}
zc::Maybe<const identity::ImplIdentityRecord&> PreAdmissionIdentityCandidate::implRecord()
    const noexcept {
  ZC_IF_SOME(value, impl->identity.tryGet<detail::ImplCandidateIdentity>()) { return value.record; }
  return zc::none;
}
zc::Maybe<const identity::OverloadHeaderAuthority&> PreAdmissionIdentityCandidate::overloadHeader()
    const noexcept {
  ZC_IF_SOME(value, impl->overloadHeader) { return value; }
  return zc::none;
}
const IdentitySyntaxSiteKey& PreAdmissionIdentityCandidate::site() const noexcept {
  return impl->site;
}
zc::ArrayPtr<const DuplicateBoundOccurrence> PreAdmissionIdentityCandidate::duplicateBounds()
    const noexcept {
  return impl->duplicateBounds.asPtr();
}
void PreAdmissionIdentityCandidate::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  if (impl->identity.is<detail::DefinitionCandidateIdentity>()) {
    impl->identity.get<detail::DefinitionCandidateIdentity>().record.encode(encoder);
  } else {
    impl->identity.get<detail::ImplCandidateIdentity>().record.encode(encoder);
  }
  ZC_IF_SOME(authority, impl->overloadHeader) {
    encoder.encodeSome();
    encodeOverloadHeaderAuthority(encoder, authority);
  } else {
    encoder.encodeNone();
  }
  impl->site.encode(encoder);
  encoder.encodeSequenceSize(impl->duplicateBounds.size());
  for (const auto& duplicate : impl->duplicateBounds) { duplicate.encode(encoder); }
}
zc::Array<uint8_t> PreAdmissionIdentityCandidate::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

ImplSourceOccurrenceKey::ImplSourceOccurrenceKey(
    zc::Own<detail::ImplSourceOccurrenceKeyData>&& value) noexcept
    : impl(zc::mv(value)) {}
ImplSourceOccurrenceKey::~ImplSourceOccurrenceKey() noexcept(false) = default;
ImplSourceOccurrenceKey::ImplSourceOccurrenceKey(ImplSourceOccurrenceKey&&) noexcept = default;
ImplSourceOccurrenceKey& ImplSourceOccurrenceKey::operator=(ImplSourceOccurrenceKey&&) noexcept =
    default;
ImplSourceOccurrenceKey ImplSourceOccurrenceKey::from(identity::ImplKey&& implementation,
                                                      IdentitySyntaxSiteKey&& site) {
  return ImplSourceOccurrenceKey(zc::heap<detail::ImplSourceOccurrenceKeyData>(
      detail::ImplSourceOccurrenceKeyData{zc::mv(implementation), zc::mv(site)}));
}
zc::Maybe<ImplSourceOccurrenceKey> ImplSourceOccurrenceKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  identity::CanonicalDecoder decoder(encoded);
  auto digest = decoder.decodeDigest();
  if (digest == zc::none) { return zc::none; }
  auto site = IdentitySyntaxSiteKey::decodeCanonical(decoder);
  if (site == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(digestValue, digest) {
    auto implementation = identity::ImplKey::fromBytes(digestValue.bytes());
    if (implementation == zc::none) { return zc::none; }
    ZC_IF_SOME(implementationValue, implementation) {
      ZC_IF_SOME(siteValue, site) { return from(zc::mv(implementationValue), zc::mv(siteValue)); }
    }
  }
  return zc::none;
}
ImplSourceOccurrenceKey ImplSourceOccurrenceKey::clone() const {
  return from(impl->implementation.clone(), impl->site.clone());
}
const identity::ImplKey& ImplSourceOccurrenceKey::implementation() const noexcept {
  return impl->implementation;
}
const IdentitySyntaxSiteKey& ImplSourceOccurrenceKey::site() const noexcept { return impl->site; }
bool ImplSourceOccurrenceKey::sameAs(const ImplSourceOccurrenceKey& other) const {
  return impl->implementation == other.impl->implementation && impl->site.sameAs(other.impl->site);
}
void ImplSourceOccurrenceKey::encode(identity::CanonicalEncoder& encoder) const {
  impl->implementation.encode(encoder);
  impl->site.encode(encoder);
}
zc::Array<uint8_t> ImplSourceOccurrenceKey::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

ImplIdentityOccurrenceGroup::ImplIdentityOccurrenceGroup(
    zc::Own<detail::ImplIdentityOccurrenceGroupData>&& value) noexcept
    : impl(zc::mv(value)) {}
ImplIdentityOccurrenceGroup::~ImplIdentityOccurrenceGroup() noexcept(false) = default;
ImplIdentityOccurrenceGroup::ImplIdentityOccurrenceGroup(ImplIdentityOccurrenceGroup&&) noexcept =
    default;
ImplIdentityOccurrenceGroup& ImplIdentityOccurrenceGroup::operator=(
    ImplIdentityOccurrenceGroup&&) noexcept = default;

zc::Maybe<ImplIdentityOccurrenceGroup> ImplIdentityOccurrenceGroup::from(
    const identity::ImplRegistry& authorities, identity::ImplId authority,
    zc::Vector<ImplSourceOccurrenceKey>&& occurrences,
    zc::ArrayPtr<const IdentitySyntaxSite> sites) {
  auto authorityValue = authorities.lookupAuthority(authority);
  if (authorityValue == zc::none || occurrences.empty()) { return zc::none; }

  ZC_IF_SOME(authorityRecord, authorityValue) {
    if (!authorityRecord.verify()) { return zc::none; }
    const IdentitySyntaxSite* previousSite = nullptr;
    for (const auto& occurrence : occurrences) {
      if (occurrence.implementation() != authorityRecord.key() ||
          !sameModule(occurrence.site().module(), authorityRecord.record().module())) {
        return zc::none;
      }
      auto resolved = resolveSite(occurrence.site(), sites);
      if (resolved == zc::none) { return zc::none; }
      ZC_IF_SOME(site, resolved) {
        if (previousSite != nullptr && !previousSite->sourceOrderLessThan(site)) {
          return zc::none;
        }
        previousSite = &site;
      }
    }
    return ImplIdentityOccurrenceGroup(
        zc::heap<detail::ImplIdentityOccurrenceGroupData>(detail::ImplIdentityOccurrenceGroupData{
            authorityRecord.key().clone(), authority, zc::mv(occurrences)}));
  }
  ZC_UNREACHABLE
}
ImplIdentityOccurrenceGroup ImplIdentityOccurrenceGroup::clone() const {
  zc::Vector<ImplSourceOccurrenceKey> occurrences(impl->occurrences.size());
  for (const auto& occurrence : impl->occurrences) { occurrences.add(occurrence.clone()); }
  return ImplIdentityOccurrenceGroup(
      zc::heap<detail::ImplIdentityOccurrenceGroupData>(detail::ImplIdentityOccurrenceGroupData{
          impl->implementation.clone(), impl->authority, zc::mv(occurrences)}));
}
const identity::ImplKey& ImplIdentityOccurrenceGroup::implementation() const noexcept {
  return impl->implementation;
}
identity::ImplId ImplIdentityOccurrenceGroup::authority() const noexcept { return impl->authority; }
zc::ArrayPtr<const ImplSourceOccurrenceKey> ImplIdentityOccurrenceGroup::occurrences()
    const noexcept {
  return impl->occurrences.asPtr();
}

}  // namespace zomlang::compiler::binder
