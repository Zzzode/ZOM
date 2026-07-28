// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/vector.h"
#include "zomlang/compiler/driver/contextual-binding-key.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Verified key and complete record pair for the active authority projection.
class ActiveDefinitionAuthorityRecord final {
public:
  ActiveDefinitionAuthorityRecord(ActiveDefinitionAuthorityRecord&&) noexcept = default;
  ActiveDefinitionAuthorityRecord& operator=(ActiveDefinitionAuthorityRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ActiveDefinitionAuthorityRecord);

  ZC_NODISCARD static zc::Maybe<ActiveDefinitionAuthorityRecord> from(
      identity::DefinitionKey&& key, identity::DefinitionIdentityRecord&& record);
  ZC_NODISCARD ActiveDefinitionAuthorityRecord clone() const;
  ZC_NODISCARD const identity::DefinitionKey& key() const noexcept;
  ZC_NODISCARD const identity::DefinitionIdentityRecord& record() const noexcept;
  ZC_NODISCARD bool sameAs(const ActiveDefinitionAuthorityRecord& other) const;

private:
  ActiveDefinitionAuthorityRecord(identity::DefinitionKey&& key,
                                  identity::DefinitionIdentityRecord&& record) noexcept;

  identity::DefinitionKey keyField;
  identity::DefinitionIdentityRecord recordField;
};

/// \brief SHA-256 identity of one complete canonical active authority set.
class ActiveDefinitionAuthoritySetFingerprint final {
public:
  ActiveDefinitionAuthoritySetFingerprint(ActiveDefinitionAuthoritySetFingerprint&&) noexcept =
      default;
  ActiveDefinitionAuthoritySetFingerprint& operator=(
      ActiveDefinitionAuthoritySetFingerprint&&) noexcept = default;
  ZC_DISALLOW_COPY(ActiveDefinitionAuthoritySetFingerprint);

  ZC_NODISCARD static zc::Maybe<ActiveDefinitionAuthoritySetFingerprint> fromBytes(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ActiveDefinitionAuthoritySetFingerprint clone() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND;
  bool operator==(const ActiveDefinitionAuthoritySetFingerprint& other) const noexcept;
  bool operator!=(const ActiveDefinitionAuthoritySetFingerprint& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit ActiveDefinitionAuthoritySetFingerprint(const identity::Sha256Digest& digest) noexcept;

  identity::Sha256Digest digestField;

  friend class ActiveDefinitionAuthorityProjection;
};

/// \brief Canonically sorted, duplicate-collapsed complete active authority set.
class ActiveDefinitionAuthorityProjection final {
public:
  ActiveDefinitionAuthorityProjection(ActiveDefinitionAuthorityProjection&&) noexcept = default;
  ActiveDefinitionAuthorityProjection& operator=(ActiveDefinitionAuthorityProjection&&) noexcept =
      default;
  ZC_DISALLOW_COPY(ActiveDefinitionAuthorityProjection);

  ZC_NODISCARD static zc::Maybe<ActiveDefinitionAuthorityProjection> from(
      const CompilationRootSetQueryKey& contextRoots,
      zc::Vector<ActiveDefinitionAuthorityRecord>&& records);
  ZC_NODISCARD ActiveDefinitionAuthorityProjection clone() const;
  ZC_NODISCARD zc::ArrayPtr<const ActiveDefinitionAuthorityRecord> records() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const ActiveDefinitionAuthoritySetFingerprint& fingerprint() const noexcept;

private:
  ActiveDefinitionAuthorityProjection(
      zc::Vector<ActiveDefinitionAuthorityRecord>&& records,
      ActiveDefinitionAuthoritySetFingerprint&& fingerprint) noexcept;

  zc::Vector<ActiveDefinitionAuthorityRecord> recordFields;
  ActiveDefinitionAuthoritySetFingerprint fingerprintField;
};

/// \brief Complete active definition record addressed by its stable digest.
struct ActiveDefinitionAuthorityInput final {
  using Key = ContextualDefinitionKey;
  using Value = identity::DefinitionIdentityRecord;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Completeness marker for one atomically installed authority projection.
struct ActiveDefinitionAuthorityReadyInput final {
  using Key = CompilationRootSetQueryKey;
  using Value = ActiveDefinitionAuthoritySetFingerprint;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

ZC_NODISCARD bool registerActiveDefinitionAuthorityInputs(query::QueryDatabase& database);

}  // namespace zomlang::compiler::driver::incremental_binding_query
