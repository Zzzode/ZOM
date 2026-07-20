// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/frozen-registry.h"

namespace zomlang::compiler::binder {

namespace identity_pre_admission_detail {
struct IdentitySyntaxSiteKeyData;
struct IdentitySyntaxSiteData;
struct DuplicateBoundOccurrenceData;
struct PreAdmissionIdentityCandidateData;
struct ImplSourceOccurrenceKeyData;
struct ImplIdentityOccurrenceGroupData;
}  // namespace identity_pre_admission_detail

/// \brief Revision-local structural address of one identity-related syntax occurrence.
class IdentitySyntaxSiteKey final {
public:
  ~IdentitySyntaxSiteKey() noexcept(false);
  IdentitySyntaxSiteKey(IdentitySyntaxSiteKey&&) noexcept;
  IdentitySyntaxSiteKey& operator=(IdentitySyntaxSiteKey&&) noexcept;
  ZC_DISALLOW_COPY(IdentitySyntaxSiteKey);

  /// \brief Admits a source only when it belongs to the module's crate.
  ZC_NODISCARD static zc::Maybe<IdentitySyntaxSiteKey> from(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      zc::Vector<uint32_t>&& moduleSyntaxPath);
  /// \brief Decodes one bounded canonical syntax-site key from an outer decoder.
  ZC_NODISCARD static zc::Maybe<IdentitySyntaxSiteKey> decodeCanonical(
      identity::CanonicalDecoder& decoder);
  ZC_NODISCARD IdentitySyntaxSiteKey clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> moduleSyntaxPath() const noexcept;
  ZC_NODISCARD bool sameAs(const IdentitySyntaxSiteKey& other) const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit IdentitySyntaxSiteKey(
      zc::Own<identity_pre_admission_detail::IdentitySyntaxSiteKeyData>&& impl) noexcept;

  zc::Own<identity_pre_admission_detail::IdentitySyntaxSiteKeyData> impl;
};

/// \brief One current source range resolved for an identity syntax site key.
class IdentitySyntaxSite final {
public:
  ~IdentitySyntaxSite() noexcept(false);
  IdentitySyntaxSite(IdentitySyntaxSite&&) noexcept;
  IdentitySyntaxSite& operator=(IdentitySyntaxSite&&) noexcept;
  ZC_DISALLOW_COPY(IdentitySyntaxSite);

  /// \brief Admits a range only when its source equals the key's source.
  ZC_NODISCARD static zc::Maybe<IdentitySyntaxSite> from(IdentitySyntaxSiteKey&& key,
                                                         identity::SourceSpan&& range);
  ZC_NODISCARD IdentitySyntaxSite clone() const;
  ZC_NODISCARD const IdentitySyntaxSiteKey& key() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& range() const noexcept;
  ZC_NODISCARD bool sourceOrderLessThan(const IdentitySyntaxSite& other) const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit IdentitySyntaxSite(
      zc::Own<identity_pre_admission_detail::IdentitySyntaxSiteData>&& impl) noexcept;

  zc::Own<identity_pre_admission_detail::IdentitySyntaxSiteData> impl;
};

/// \brief One normalized bound removed after its first current source occurrence.
class DuplicateBoundOccurrence final {
public:
  ~DuplicateBoundOccurrence() noexcept(false);
  DuplicateBoundOccurrence(DuplicateBoundOccurrence&&) noexcept;
  DuplicateBoundOccurrence& operator=(DuplicateBoundOccurrence&&) noexcept;
  ZC_DISALLOW_COPY(DuplicateBoundOccurrence);

  /// \brief Requires distinct first and duplicate sites in one module and source.
  ZC_NODISCARD static zc::Maybe<DuplicateBoundOccurrence> from(
      identity::CanonicalBoundObligation&& obligation, IdentitySyntaxSiteKey&& first,
      IdentitySyntaxSiteKey&& duplicate);
  ZC_NODISCARD DuplicateBoundOccurrence clone() const;
  ZC_NODISCARD const identity::CanonicalBoundObligation& obligation() const noexcept;
  ZC_NODISCARD const IdentitySyntaxSiteKey& first() const noexcept;
  ZC_NODISCARD const IdentitySyntaxSiteKey& duplicate() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit DuplicateBoundOccurrence(
      zc::Own<identity_pre_admission_detail::DuplicateBoundOccurrenceData>&& impl) noexcept;

  zc::Own<identity_pre_admission_detail::DuplicateBoundOccurrenceData> impl;
};

enum class PreAdmissionIdentityKind : uint8_t { Definition = 0x01, Implementation = 0x02 };

/// \brief Complete revision-local candidate retained before semantic handle admission.
class PreAdmissionIdentityCandidate final {
public:
  ~PreAdmissionIdentityCandidate() noexcept(false);
  PreAdmissionIdentityCandidate(PreAdmissionIdentityCandidate&&) noexcept;
  PreAdmissionIdentityCandidate& operator=(PreAdmissionIdentityCandidate&&) noexcept;
  ZC_DISALLOW_COPY(PreAdmissionIdentityCandidate);

  /// \brief Validates the definition record, overload authority, site, and duplicate bounds.
  ZC_NODISCARD static zc::Maybe<PreAdmissionIdentityCandidate> definition(
      identity::DefinitionIdentityRecord&& record,
      zc::Maybe<identity::OverloadHeaderAuthority>&& overloadHeader, IdentitySyntaxSiteKey&& site,
      zc::Vector<DuplicateBoundOccurrence>&& duplicateBounds);
  /// \brief Validates the implementation record and requires an absent overload authority.
  ZC_NODISCARD static zc::Maybe<PreAdmissionIdentityCandidate> implementation(
      identity::ImplIdentityRecord&& record,
      zc::Maybe<identity::OverloadHeaderAuthority>&& overloadHeader, IdentitySyntaxSiteKey&& site,
      zc::Vector<DuplicateBoundOccurrence>&& duplicateBounds);
  ZC_NODISCARD PreAdmissionIdentityCandidate clone() const;
  ZC_NODISCARD PreAdmissionIdentityKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::DefinitionIdentityRecord&> definitionRecord()
      const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::ImplIdentityRecord&> implRecord() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::OverloadHeaderAuthority&> overloadHeader() const noexcept;
  ZC_NODISCARD const IdentitySyntaxSiteKey& site() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DuplicateBoundOccurrence> duplicateBounds() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit PreAdmissionIdentityCandidate(
      zc::Own<identity_pre_admission_detail::PreAdmissionIdentityCandidateData>&& impl) noexcept;

  zc::Own<identity_pre_admission_detail::PreAdmissionIdentityCandidateData> impl;
};

/// \brief Stable implementation key paired with one revision-local source site.
class ImplSourceOccurrenceKey final {
public:
  ~ImplSourceOccurrenceKey() noexcept(false);
  ImplSourceOccurrenceKey(ImplSourceOccurrenceKey&&) noexcept;
  ImplSourceOccurrenceKey& operator=(ImplSourceOccurrenceKey&&) noexcept;
  ZC_DISALLOW_COPY(ImplSourceOccurrenceKey);

  ZC_NODISCARD static ImplSourceOccurrenceKey from(identity::ImplKey&& implementation,
                                                   IdentitySyntaxSiteKey&& site);
  /// \brief Decodes one complete bounded implementation occurrence key.
  ZC_NODISCARD static zc::Maybe<ImplSourceOccurrenceKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  ZC_NODISCARD ImplSourceOccurrenceKey clone() const;
  ZC_NODISCARD const identity::ImplKey& implementation() const noexcept;
  ZC_NODISCARD const IdentitySyntaxSiteKey& site() const noexcept;
  ZC_NODISCARD bool sameAs(const ImplSourceOccurrenceKey& other) const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit ImplSourceOccurrenceKey(
      zc::Own<identity_pre_admission_detail::ImplSourceOccurrenceKeyData>&& impl) noexcept;

  zc::Own<identity_pre_admission_detail::ImplSourceOccurrenceKeyData> impl;
};

/// \brief One shared semantic implementation authority and all current source occurrences.
class ImplIdentityOccurrenceGroup final {
public:
  ~ImplIdentityOccurrenceGroup() noexcept(false);
  ImplIdentityOccurrenceGroup(ImplIdentityOccurrenceGroup&&) noexcept;
  ImplIdentityOccurrenceGroup& operator=(ImplIdentityOccurrenceGroup&&) noexcept;
  ZC_DISALLOW_COPY(ImplIdentityOccurrenceGroup);

  /// \brief Requires a valid authority handle and a non-empty, source-sorted exact site set.
  /// \param authorities Frozen registry used to expand and validate `authority`.
  /// \param authority Revision-local handle issued by `authorities`.
  /// \param occurrences Occurrences in canonical source, range, then structural-path order.
  /// \param sites Current site inventory used to resolve every occurrence exactly once.
  ZC_NODISCARD static zc::Maybe<ImplIdentityOccurrenceGroup> from(
      const identity::ImplRegistry& authorities, identity::ImplId authority,
      zc::Vector<ImplSourceOccurrenceKey>&& occurrences,
      zc::ArrayPtr<const IdentitySyntaxSite> sites);
  ZC_NODISCARD ImplIdentityOccurrenceGroup clone() const;
  ZC_NODISCARD const identity::ImplKey& implementation() const noexcept;
  ZC_NODISCARD identity::ImplId authority() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ImplSourceOccurrenceKey> occurrences() const noexcept;

private:
  explicit ImplIdentityOccurrenceGroup(
      zc::Own<identity_pre_admission_detail::ImplIdentityOccurrenceGroupData>&& impl) noexcept;

  zc::Own<identity_pre_admission_detail::ImplIdentityOccurrenceGroupData> impl;
};

}  // namespace zomlang::compiler::binder
