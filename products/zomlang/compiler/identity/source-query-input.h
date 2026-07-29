// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zomlang/compiler/identity/crate-key.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::package {
class VerifiedPackageCompilationRequest;
}

namespace zomlang::compiler::identity {
class ImmutableSourceSnapshot;
class SourceFileKey;

namespace source_query {

class StableSourceQueryKey final {
public:
  StableSourceQueryKey(StableSourceQueryKey&&) noexcept = default;
  StableSourceQueryKey& operator=(StableSourceQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableSourceQueryKey);

  ZC_NODISCARD static zc::Maybe<StableSourceQueryKey> fromVerified(const SourceFileKey& source);
  ZC_NODISCARD static zc::Maybe<StableSourceQueryKey> decodeBounded(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD StableSourceQueryKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalSourceBytes() const ZC_LIFETIMEBOUND;
  bool operator==(const StableSourceQueryKey& other) const noexcept;
  bool operator!=(const StableSourceQueryKey& other) const noexcept { return !(*this == other); }
  bool operator<(const StableSourceQueryKey& other) const noexcept;

private:
  explicit StableSourceQueryKey(zc::Array<uint8_t>&& canonicalSourceBytes) noexcept;
  zc::Array<uint8_t> canonicalSourceBytesField;
};

class CanonicalSourceSnapshot final {
public:
  CanonicalSourceSnapshot(CanonicalSourceSnapshot&&) noexcept = default;
  CanonicalSourceSnapshot& operator=(CanonicalSourceSnapshot&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalSourceSnapshot);

  ZC_NODISCARD static zc::Maybe<CanonicalSourceSnapshot> fromVerified(
      const ImmutableSourceSnapshot& snapshot);
  ZC_NODISCARD static zc::Maybe<CanonicalSourceSnapshot> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CanonicalSourceSnapshot clone() const;
  ZC_NODISCARD const Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CanonicalSourceSnapshot& other) const noexcept;
  bool operator!=(const CanonicalSourceSnapshot& other) const noexcept { return !(*this == other); }

private:
  CanonicalSourceSnapshot(const Sha256Digest& contentDigest, zc::Array<uint8_t>&& bytes) noexcept;
  Sha256Digest contentDigestField;
  zc::Array<uint8_t> bytesField;
};

class CanonicalCompilationOptions final {
public:
  CanonicalCompilationOptions(CanonicalCompilationOptions&&) noexcept = default;
  CanonicalCompilationOptions& operator=(CanonicalCompilationOptions&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalCompilationOptions);

  ZC_NODISCARD static zc::Maybe<CanonicalCompilationOptions> fromVerified(
      const driver::package::VerifiedPackageCompilationRequest& request);
  ZC_NODISCARD static zc::Maybe<CanonicalCompilationOptions> fromCanonicalSelections(
      zc::Array<uint8_t>&& hostTarget, zc::Array<uint8_t>&& target, bool useUnicode,
      bool allowDollarIdentifiers, bool supportRegexLiterals);
  ZC_NODISCARD static zc::Maybe<CanonicalCompilationOptions> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CanonicalCompilationOptions clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> hostTargetBytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> targetBytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD bool useUnicode() const noexcept;
  ZC_NODISCARD bool allowDollarIdentifiers() const noexcept;
  ZC_NODISCARD bool supportRegexLiterals() const noexcept;
  /// \brief Verifies that this complete session option record selects one exact crate.
  ZC_NODISCARD bool matchesCrate(const CrateKey& crate) const;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CanonicalCompilationOptions& other) const noexcept;
  bool operator!=(const CanonicalCompilationOptions& other) const noexcept {
    return !(*this == other);
  }

private:
  CanonicalCompilationOptions(zc::Array<uint8_t>&& hostTarget, zc::Array<uint8_t>&& target,
                              bool useUnicode, bool allowDollarIdentifiers,
                              bool supportRegexLiterals) noexcept;
  zc::Array<uint8_t> hostTargetField;
  zc::Array<uint8_t> targetField;
  bool useUnicodeField;
  bool allowDollarIdentifiersField;
  bool supportRegexLiteralsField;
};

struct CompilationOptionsInput final {
  using Key = CrateKey;
  using Value = CanonicalCompilationOptions;

  static constexpr query::InputDescriptorMetadata descriptor{"CompilationOptionsInput"_zcc,
                                                             "zom.query.compilation-options"_zcc,
                                                             query::Durability::Medium};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

struct SourceSnapshotInput final {
  using Key = StableSourceQueryKey;
  using Value = CanonicalSourceSnapshot;

  static constexpr query::InputDescriptorMetadata descriptor{
      "SourceSnapshotInput"_zcc, "zom.query.source-snapshot"_zcc, query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

ZC_NODISCARD bool registerSourceQueryInputs(query::QueryDatabase& database);

}  // namespace source_query
}  // namespace zomlang::compiler::identity
