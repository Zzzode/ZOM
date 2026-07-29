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

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/refcount.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::identity {
class Sha256Digest;
}

namespace zomlang::compiler::query {

class QueryDatabase;
class QuerySnapshot;
class QueryContext;
class InputTransaction;
template <typename Spec>
class CapabilityResultDecoder;
template <typename Descriptor>
class CapabilityCandidateContract;
template <typename ContextRoots, typename FinalWitness>
class FinalSnapshotSeal;

namespace test {
class QueryRuntimeTestAccess;
}

namespace _query_detail {
class QueryDatabaseIdentityToken;
class CapabilityMemoBuilderBase;
template <typename Capability>
class CapabilityMemoBuilder;
class QueryRequestResultAccess;
}  // namespace _query_detail

/// \brief Process-local coordinate of one committed explicit-input root.
class DatabaseRevision final {
public:
  constexpr DatabaseRevision() noexcept = default;
  explicit constexpr DatabaseRevision(uint64_t value) noexcept : valueField(value) {}

  ZC_NODISCARD constexpr uint64_t value() const noexcept { return valueField; }
  ZC_NODISCARD constexpr DatabaseRevision next() const noexcept {
    return DatabaseRevision(valueField + 1);
  }

  constexpr bool operator==(DatabaseRevision other) const noexcept {
    return valueField == other.valueField;
  }
  constexpr bool operator!=(DatabaseRevision other) const noexcept { return !(*this == other); }
  constexpr bool operator<(DatabaseRevision other) const noexcept {
    return valueField < other.valueField;
  }
  constexpr bool operator<=(DatabaseRevision other) const noexcept {
    return valueField <= other.valueField;
  }
  constexpr bool operator>(DatabaseRevision other) const noexcept { return other < *this; }

private:
  uint64_t valueField = 0;
};

/// \brief Closed control failures for input transactions and final sealing.
enum class InputTransactionFailure : uint8_t {
  TransactionAlreadyOpen = 0x01,
  TransactionClosed = 0x02,
  InputMutationAfterFinalSeal = 0x03,
  UnknownDescriptor = 0x04,
  DescriptorKindMismatch = 0x05,
  InvalidKeyEncoding = 0x06,
  FingerprintCollision = 0x07,
  FrozenInputMutation = 0x08,
  MissingInputForErase = 0x09,
  DuplicateInputOperation = 0x0a,
  StaleBaseRevision = 0x0b,
  RevisionExhausted = 0x0c,
  OpenTransactionDuringFinalSeal = 0x0d,
  FinalSealAlreadyPublished = 0x0e,
  ForeignSnapshot = 0x0f,
  StaleSnapshot = 0x10,
  InvalidFinalSealAuthority = 0x11
};

enum class InputMutationResultKind : uint8_t { Applied = 0x01, Rejected = 0x02 };

namespace _query_detail {

class InputMutationApplied final {};

class InputCommitCommitted final {
public:
  explicit InputCommitCommitted(DatabaseRevision revision) noexcept : revisionField(revision) {}

  DatabaseRevision revisionField;
};

}  // namespace _query_detail

/// \brief Closed result of staging one input mutation.
class InputMutationResult final {
public:
  InputMutationResult(InputMutationResult&&) noexcept = default;
  InputMutationResult& operator=(InputMutationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(InputMutationResult);

  ZC_NODISCARD static InputMutationResult applied() noexcept {
    return InputMutationResult(_query_detail::InputMutationApplied());
  }
  ZC_NODISCARD static InputMutationResult rejected(InputTransactionFailure failure) noexcept {
    return InputMutationResult(failure);
  }

  ZC_NODISCARD InputMutationResultKind kind() const noexcept {
    return isApplied() ? InputMutationResultKind::Applied : InputMutationResultKind::Rejected;
  }
  ZC_NODISCARD bool isApplied() const noexcept {
    return storageField.template is<_query_detail::InputMutationApplied>();
  }
  ZC_NODISCARD InputTransactionFailure failure() const {
    ZC_IREQUIRE(!isApplied(), "applied input mutation has no failure");
    return storageField.template get<InputTransactionFailure>();
  }

private:
  template <typename Alternative>
  explicit InputMutationResult(Alternative&& alternative) noexcept
      : storageField(zc::fwd<Alternative>(alternative)) {}

  zc::OneOf<_query_detail::InputMutationApplied, InputTransactionFailure> storageField;
};

enum class InputCommitResultKind : uint8_t { Committed = 0x01, Rejected = 0x02 };

/// \brief Closed result of committing one input transaction.
class InputCommitResult final {
public:
  InputCommitResult(InputCommitResult&&) noexcept = default;
  InputCommitResult& operator=(InputCommitResult&&) noexcept = default;
  ZC_DISALLOW_COPY(InputCommitResult);

  ZC_NODISCARD static InputCommitResult committed(DatabaseRevision revision) noexcept {
    return InputCommitResult(_query_detail::InputCommitCommitted(revision));
  }
  ZC_NODISCARD static InputCommitResult rejected(InputTransactionFailure failure) noexcept {
    return InputCommitResult(failure);
  }

  ZC_NODISCARD InputCommitResultKind kind() const noexcept {
    return isCommitted() ? InputCommitResultKind::Committed : InputCommitResultKind::Rejected;
  }
  ZC_NODISCARD bool isCommitted() const noexcept {
    return storageField.template is<_query_detail::InputCommitCommitted>();
  }
  ZC_NODISCARD DatabaseRevision revision() const {
    ZC_IREQUIRE(isCommitted(), "rejected input commit has no revision");
    return storageField.template get<_query_detail::InputCommitCommitted>().revisionField;
  }
  ZC_NODISCARD InputTransactionFailure failure() const {
    ZC_IREQUIRE(!isCommitted(), "committed input transaction has no failure");
    return storageField.template get<InputTransactionFailure>();
  }

private:
  template <typename Alternative>
  explicit InputCommitResult(Alternative&& alternative) noexcept
      : storageField(zc::fwd<Alternative>(alternative)) {}

  zc::OneOf<_query_detail::InputCommitCommitted, InputTransactionFailure> storageField;
};

/// \brief Process-local identity of one query database instance.
class QueryDatabaseIdentity final {
public:
  QueryDatabaseIdentity(QueryDatabaseIdentity&&) noexcept;
  QueryDatabaseIdentity& operator=(QueryDatabaseIdentity&&) noexcept;
  ~QueryDatabaseIdentity() noexcept(false);
  ZC_DISALLOW_COPY(QueryDatabaseIdentity);

  bool operator==(const QueryDatabaseIdentity& other) const noexcept;
  bool operator!=(const QueryDatabaseIdentity& other) const noexcept { return !(*this == other); }

private:
  explicit QueryDatabaseIdentity(
      zc::Arc<const _query_detail::QueryDatabaseIdentityToken>&& token) noexcept;

  ZC_NODISCARD static QueryDatabaseIdentity create();
  ZC_NODISCARD QueryDatabaseIdentity retain() const;

  zc::Arc<const _query_detail::QueryDatabaseIdentityToken> tokenField;

  friend class QueryDatabase;
  friend class QueryContext;
  friend class QuerySnapshot;
  friend class InputTransaction;
  friend class RevisionLocalCapabilityMemoBase;
  template <typename ContextRoots, typename FinalWitness>
  friend class FinalSnapshotSeal;
};

/// \brief Move-only proof that one exact database snapshot has final immutable inputs.
template <typename ContextRoots, typename FinalWitness = identity::Sha256Digest>
class FinalSnapshotSeal final {
public:
  FinalSnapshotSeal(FinalSnapshotSeal&&) noexcept = default;
  FinalSnapshotSeal& operator=(FinalSnapshotSeal&&) noexcept = default;
  ZC_DISALLOW_COPY(FinalSnapshotSeal);

  ZC_NODISCARD const QueryDatabaseIdentity& database() const ZC_LIFETIMEBOUND {
    return databaseField;
  }
  ZC_NODISCARD DatabaseRevision revision() const noexcept { return revisionField; }
  ZC_NODISCARD const ContextRoots& contextRoots() const ZC_LIFETIMEBOUND {
    return contextRootsField;
  }
  ZC_NODISCARD const FinalWitness& finalWitness() const ZC_LIFETIMEBOUND {
    return finalWitnessField;
  }

  bool operator==(const FinalSnapshotSeal& other) const noexcept {
    return databaseField == other.databaseField && revisionField == other.revisionField &&
           contextRootsField == other.contextRootsField &&
           finalWitnessField == other.finalWitnessField;
  }
  bool operator!=(const FinalSnapshotSeal& other) const noexcept { return !(*this == other); }

private:
  FinalSnapshotSeal(QueryDatabaseIdentity&& database, DatabaseRevision revision,
                    ContextRoots&& contextRoots, const FinalWitness& finalWitness) noexcept
      : databaseField(zc::mv(database)),
        revisionField(revision),
        contextRootsField(zc::mv(contextRoots)),
        finalWitnessField(finalWitness) {}

  QueryDatabaseIdentity databaseField;
  DatabaseRevision revisionField;
  ContextRoots contextRootsField;
  FinalWitness finalWitnessField;

  friend class QueryDatabase;
};

enum class FinalSealResultKind : uint8_t { Sealed = 0x01, Rejected = 0x02 };

/// \brief Closed result of publishing one irreversible final snapshot seal.
template <typename ContextRoots, typename FinalWitness = identity::Sha256Digest>
class FinalSealResult final {
public:
  FinalSealResult(FinalSealResult&&) noexcept = default;
  FinalSealResult& operator=(FinalSealResult&&) noexcept = default;
  ZC_DISALLOW_COPY(FinalSealResult);

  ZC_NODISCARD static FinalSealResult sealed(FinalSnapshotSeal<ContextRoots, FinalWitness>&& seal) {
    return FinalSealResult(FinalSealResultKind::Sealed, zc::mv(seal));
  }
  ZC_NODISCARD static FinalSealResult rejected(InputTransactionFailure failure) {
    return FinalSealResult(FinalSealResultKind::Rejected, failure);
  }

  ZC_NODISCARD FinalSealResultKind kind() const noexcept { return kindField; }
  ZC_NODISCARD bool isSealed() const noexcept { return kindField == FinalSealResultKind::Sealed; }
  ZC_NODISCARD const FinalSnapshotSeal<ContextRoots, FinalWitness>& seal() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isSealed(), "rejected final seal result has no seal");
    return storageField.template get<FinalSnapshotSeal<ContextRoots, FinalWitness>>();
  }
  ZC_NODISCARD FinalSnapshotSeal<ContextRoots, FinalWitness> takeSeal() && {
    ZC_IREQUIRE(isSealed(), "rejected final seal result has no seal");
    return zc::mv(storageField).template get<FinalSnapshotSeal<ContextRoots, FinalWitness>>();
  }
  ZC_NODISCARD InputTransactionFailure failure() const {
    ZC_IREQUIRE(!isSealed(), "sealed final result has no failure");
    return storageField.template get<InputTransactionFailure>();
  }

private:
  template <typename Alternative>
  FinalSealResult(FinalSealResultKind kind, Alternative&& alternative)
      : kindField(kind), storageField(zc::fwd<Alternative>(alternative)) {}

  FinalSealResultKind kindField;
  zc::OneOf<FinalSnapshotSeal<ContextRoots, FinalWitness>, InputTransactionFailure> storageField;
};

/// \brief Independent verification result for final-context authority.
enum class FinalAuthorityCheck : uint8_t { Verified = 0x01, Rejected = 0x02 };

/// \brief Closed active-membership alternatives for one complete authority record.
enum class ActiveMembershipResultKind : uint8_t { Active = 0x01, Inactive = 0x02 };

namespace _query_detail {

template <typename Record>
class ActiveMembershipRecord final {
public:
  explicit ActiveMembershipRecord(Record&& record) : recordField(zc::mv(record)) {}

  Record recordField;
};

class InactiveMembership final {};

}  // namespace _query_detail

/// \brief Exact active authority or deterministic inactive membership.
template <typename Record>
class ActiveMembershipResult final {
public:
  ActiveMembershipResult(ActiveMembershipResult&&) noexcept = default;
  ActiveMembershipResult& operator=(ActiveMembershipResult&&) noexcept = default;
  ZC_DISALLOW_COPY(ActiveMembershipResult);

  ZC_NODISCARD static ActiveMembershipResult active(Record&& record) {
    return ActiveMembershipResult(_query_detail::ActiveMembershipRecord<Record>(zc::mv(record)));
  }
  ZC_NODISCARD static ActiveMembershipResult inactive() {
    return ActiveMembershipResult(_query_detail::InactiveMembership());
  }

  ZC_NODISCARD bool isActive() const noexcept {
    return storageField.template is<_query_detail::ActiveMembershipRecord<Record>>();
  }
  ZC_NODISCARD ActiveMembershipResultKind kind() const noexcept {
    return isActive() ? ActiveMembershipResultKind::Active : ActiveMembershipResultKind::Inactive;
  }
  ZC_NODISCARD const Record& record() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isActive(), "inactive membership has no authority record");
    return storageField.template get<_query_detail::ActiveMembershipRecord<Record>>().recordField;
  }

private:
  template <typename Alternative>
  explicit ActiveMembershipResult(Alternative&& alternative)
      : storageField(zc::fwd<Alternative>(alternative)) {}

  zc::OneOf<_query_detail::ActiveMembershipRecord<Record>, _query_detail::InactiveMembership>
      storageField;
};

/// \brief Closed validation durability lattice ordered from mutable to immutable.
enum class Durability : uint8_t { Low = 0, Medium = 1, High = 2, Frozen = 3 };

/// \brief Cross-revision reuse contract of one query kind.
enum class ReuseClass : uint8_t {
  Input = 0x01,
  RevisionLocal = 0x02,
  Semantic = 0x03,
  Persisted = 0x04
};

/// \brief Retention policy for the complete equality witness.
enum class RetentionClass : uint8_t { Retained = 0, Evictable = 1 };

/// \brief Snapshot admission required before a capability evaluator may run.
enum class CapabilityAdmission : uint8_t { AnySnapshot = 0x01, FinalSealedSnapshot = 0x02 };

/// \brief Closed descriptor kinds selected by literal metadata type.
enum class QueryDescriptorKind : uint8_t {
  Input = 0x01,
  Semantic = 0x02,
  RevisionLocalCapability = 0x03
};

/// \brief Compile-time role of one descriptor inventory row.
enum class QueryDescriptorRole : uint8_t { Ordinary = 0x01, CompleteContextAuthority = 0x02 };

/// \brief Canonical semantic equality policy.
enum class QueryEqualityPolicy : uint8_t { CanonicalBytes = 0x01 };

/// \brief Closed cycle policy.
enum class QueryCyclePolicy : uint8_t { Reject = 0x01 };

/// \brief Declared asymptotic provider cost.
enum class QueryCostClass : uint8_t { Linear = 0x01 };

/// \brief Literal metadata for one input descriptor.
struct InputDescriptorMetadata final {
  zc::LiteralStringConst name;
  zc::LiteralStringConst domain;
  Durability durability;
};

/// \brief Literal metadata for one reusable semantic descriptor.
struct SemanticDescriptorMetadata final {
  zc::LiteralStringConst name;
  zc::LiteralStringConst domain;
  ReuseClass reuse;
  RetentionClass retention;
  QueryEqualityPolicy equality;
  QueryCyclePolicy cycle;
  QueryCostClass cost;
};

/// \brief Literal metadata for one revision-local capability descriptor.
struct CapabilityDescriptorMetadata final {
  zc::LiteralStringConst name;
  zc::LiteralStringConst domain;
  RetentionClass retention;
  QueryCyclePolicy cycle;
  QueryCostClass cost;
  CapabilityAdmission admission;
};

/// \brief Compile-time shape of one input query descriptor.
template <typename Descriptor>
concept InputQueryDescriptor =
    requires(const typename Descriptor::Key& key, const typename Descriptor::Value& value,
             zc::ArrayPtr<const uint8_t> bytes) {
      Descriptor::descriptor;
      Descriptor::encodeKey(key);
      Descriptor::decodeKey(bytes);
      Descriptor::encodeValue(value);
      Descriptor::decodeValue(bytes);
    } &&
    zc::isSameType<zc::RemoveConst<decltype(Descriptor::descriptor)>, InputDescriptorMetadata>() &&
    zc::isSameType<decltype(Descriptor::encodeKey(zc::instance<const typename Descriptor::Key&>())),
                   zc::Array<uint8_t>>() &&
    zc::isSameType<decltype(Descriptor::decodeKey(zc::instance<zc::ArrayPtr<const uint8_t>>())),
                   zc::Maybe<typename Descriptor::Key>>() &&
    zc::isSameType<decltype(Descriptor::encodeValue(
                       zc::instance<const typename Descriptor::Value&>())),
                   zc::Array<uint8_t>>() &&
    zc::isSameType<decltype(Descriptor::decodeValue(zc::instance<zc::ArrayPtr<const uint8_t>>())),
                   zc::Maybe<typename Descriptor::Value>>();

/// \brief Input descriptor authorized to verify the one complete final context.
template <typename Descriptor>
concept CompleteContextAuthorityInput =
    InputQueryDescriptor<Descriptor> &&
    requires(const QuerySnapshot& snapshot, const typename Descriptor::Key& key,
             const typename Descriptor::Value& value, const identity::Sha256Digest& witness) {
      Descriptor::verifyFinalAuthority(snapshot, key, value, witness);
    } &&
    zc::isSameType<decltype(Descriptor::verifyFinalAuthority(
                       zc::instance<const QuerySnapshot&>(),
                       zc::instance<const typename Descriptor::Key&>(),
                       zc::instance<const typename Descriptor::Value&>(),
                       zc::instance<const identity::Sha256Digest&>())),
                   FinalAuthorityCheck>();

/// \brief Stable target-inventory ordinal of one query descriptor.
class QueryKindId final {
public:
  explicit constexpr QueryKindId(uint32_t value) noexcept : valueField(value) {}

  ZC_NODISCARD constexpr uint32_t value() const noexcept { return valueField; }
  constexpr bool operator==(QueryKindId other) const noexcept {
    return valueField == other.valueField;
  }
  constexpr bool operator!=(QueryKindId other) const noexcept { return !(*this == other); }
  constexpr bool operator<(QueryKindId other) const noexcept {
    return valueField < other.valueField;
  }

private:
  uint32_t valueField;
};

/// \brief One immutable generated descriptor-inventory row.
struct QueryDescriptorInventoryRow final {
  uint32_t ordinal;
  zc::LiteralStringConst descriptorType;
  zc::LiteralStringConst name;
  zc::LiteralStringConst domain;
  QueryDescriptorKind kind;
  QueryDescriptorRole role;
  ReuseClass reuse;
  RetentionClass retention;
  Durability durability;
  QueryEqualityPolicy equality;
  QueryCyclePolicy cycle;
  QueryCostClass cost;
  CapabilityAdmission admission;
  zc::LiteralStringConst ownerPathFamily;
};

class QueryDescriptorInventoryRef;

template <typename GeneratedInventory>
ZC_NODISCARD constexpr QueryDescriptorInventoryRef generatedQueryDescriptorInventory();

/// \brief Immutable generated descriptor inventory selected by one build target.
class QueryDescriptorInventoryRef final {
public:
  ZC_NODISCARD constexpr zc::LiteralStringConst identity() const noexcept { return identityField; }
  ZC_NODISCARD constexpr zc::ArrayPtr<const QueryDescriptorInventoryRow> rows() const noexcept {
    return rowsField;
  }

private:
  constexpr QueryDescriptorInventoryRef(
      zc::LiteralStringConst identity,
      zc::ArrayPtr<const QueryDescriptorInventoryRow> rows) noexcept
      : identityField(identity), rowsField(rows) {}

  zc::LiteralStringConst identityField;
  zc::ArrayPtr<const QueryDescriptorInventoryRow> rowsField;

  template <typename GeneratedInventory>
  friend constexpr QueryDescriptorInventoryRef generatedQueryDescriptorInventory();
};

/// \brief Constructs a reference from one generated immutable inventory definition.
template <typename GeneratedInventory>
constexpr QueryDescriptorInventoryRef generatedQueryDescriptorInventory() {
  return QueryDescriptorInventoryRef(GeneratedInventory::identity, GeneratedInventory::rows());
}

/// \brief Generated binding from one descriptor type to one target inventory row.
template <typename Descriptor>
struct QueryDescriptorInventoryBinding;

/// \brief Closed setup-time descriptor registration failures.
enum class DescriptorRegistrationFailure : uint8_t {
  DescriptorAbsentFromInventory = 0x01,
  InventoryMismatch = 0x02,
  MetadataMismatch = 0x03,
  SlotAlreadyRegistered = 0x04,
  SlotCollision = 0x05
};

/// \brief Exact setup-time result of descriptor registration.
class DescriptorRegistrationResult final {
public:
  DescriptorRegistrationResult(DescriptorRegistrationResult&&) noexcept = default;
  DescriptorRegistrationResult& operator=(DescriptorRegistrationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(DescriptorRegistrationResult);

  ZC_NODISCARD static DescriptorRegistrationResult registered(QueryKindId kind) {
    return DescriptorRegistrationResult(kind);
  }
  ZC_NODISCARD static DescriptorRegistrationResult rejected(DescriptorRegistrationFailure failure) {
    return DescriptorRegistrationResult(failure);
  }

  ZC_NODISCARD bool isRegistered() const noexcept {
    return storageField.template is<QueryKindId>();
  }
  ZC_NODISCARD QueryKindId kind() const {
    ZC_IREQUIRE(isRegistered(), "rejected descriptor registration has no query kind");
    return storageField.template get<QueryKindId>();
  }
  ZC_NODISCARD DescriptorRegistrationFailure failure() const {
    ZC_IREQUIRE(!isRegistered(), "registered descriptor has no registration failure");
    return storageField.template get<DescriptorRegistrationFailure>();
  }

private:
  template <typename Alternative>
  explicit DescriptorRegistrationResult(Alternative&& alternative)
      : storageField(zc::fwd<Alternative>(alternative)) {}

  zc::OneOf<QueryKindId, DescriptorRegistrationFailure> storageField;
};

/// \brief SHA-256 fingerprint of a query domain and canonical key encoding.
class QueryKeyFingerprint final {
public:
  constexpr QueryKeyFingerprint() noexcept = default;

  /// \brief Constructs a fingerprint from exactly 32 digest bytes.
  ZC_NODISCARD static zc::Maybe<QueryKeyFingerprint> fromBytes(zc::ArrayPtr<const uint8_t> bytes);

  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND {
    return zc::arrayPtr(valueField);
  }

  bool operator==(const QueryKeyFingerprint& other) const noexcept {
    return bytes() == other.bytes();
  }
  bool operator!=(const QueryKeyFingerprint& other) const noexcept { return !(*this == other); }
  bool operator<(const QueryKeyFingerprint& other) const noexcept;

private:
  uint8_t valueField[32] = {};

  friend class CanonicalQueryKey;
};

/// \brief Complete query kind and canonical key identity retained beside its fingerprint.
class CanonicalQueryKey final {
public:
  CanonicalQueryKey(CanonicalQueryKey&&) noexcept = default;
  CanonicalQueryKey& operator=(CanonicalQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalQueryKey);

  ZC_NODISCARD CanonicalQueryKey clone() const;
  ZC_NODISCARD QueryKindId kind() const noexcept { return kindField; }
  ZC_NODISCARD const QueryKeyFingerprint& fingerprint() const noexcept { return fingerprintField; }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalBytes() const ZC_LIFETIMEBOUND {
    return canonicalBytesField.asPtr();
  }

  bool operator==(const CanonicalQueryKey& other) const noexcept;
  bool operator!=(const CanonicalQueryKey& other) const noexcept { return !(*this == other); }
  bool operator<(const CanonicalQueryKey& other) const noexcept;

private:
  CanonicalQueryKey(QueryKindId kind, const QueryKeyFingerprint& fingerprint,
                    zc::Array<uint8_t>&& canonicalBytes) noexcept;

  QueryKindId kindField;
  QueryKeyFingerprint fingerprintField;
  zc::Array<uint8_t> canonicalBytesField;

  friend class QueryDatabase;
};

/// \brief Deterministic semantic result alternatives that can be memoized.
enum class QueryValueKind : uint8_t { Value = 0, Absence = 1, SemanticFailure = 2 };

/// \brief Presence state observed by one tracked optional input read.
enum class InputProbeObservation : uint8_t { Present = 0, Absent = 1 };

/// \brief Immutable canonical payload of one deterministic query completion.
class QueryValue final {
public:
  QueryValue(QueryValue&&) noexcept = default;
  QueryValue& operator=(QueryValue&&) noexcept = default;
  ZC_DISALLOW_COPY(QueryValue);

  ZC_NODISCARD static QueryValue value(zc::Array<uint8_t>&& canonicalBytes);
  ZC_NODISCARD static QueryValue absence();
  ZC_NODISCARD static QueryValue semanticFailure(zc::Array<uint8_t>&& canonicalBytes);

  ZC_NODISCARD QueryValue clone() const;
  ZC_NODISCARD QueryValueKind kind() const noexcept { return kindField; }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalBytes() const ZC_LIFETIMEBOUND {
    return canonicalBytesField.asPtr();
  }

  bool operator==(const QueryValue& other) const noexcept;
  bool operator!=(const QueryValue& other) const noexcept { return !(*this == other); }

private:
  QueryValue(QueryValueKind kind, zc::Array<uint8_t>&& canonicalBytes) noexcept;

  QueryValueKind kindField;
  zc::Array<uint8_t> canonicalBytesField;
};

class SemanticContextCapabilityArena;
class SnapshotCapabilityArena;
class RevisionLocalCapabilityMemoBase;

/// \brief Descriptor-codec-owned canonical witness for one capability candidate.
class StableWitnessBytes final {
public:
  StableWitnessBytes(StableWitnessBytes&&) noexcept = default;
  StableWitnessBytes& operator=(StableWitnessBytes&&) noexcept = default;
  ZC_DISALLOW_COPY(StableWitnessBytes);

  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND {
    return bytesField.asPtr();
  }

private:
  explicit StableWitnessBytes(zc::Array<uint8_t>&& bytes) : bytesField(zc::mv(bytes)) {
    ZC_IREQUIRE(bytesField.size() != 0, "stable capability witness must not be empty");
  }

  zc::Array<uint8_t> bytesField;

  template <typename Descriptor>
  friend class CapabilityCandidateContract;
};

/// \brief Type-erased owner of the session semantic resources anchored by an arena.
///
/// The compiler-session implementation supplies one concrete owner containing
/// its semantic-context issuer, identity registries, and semantic type store.
class SemanticContextCapabilityResources {
public:
  virtual ~SemanticContextCapabilityResources() noexcept(false) = default;
  ZC_DISALLOW_COPY_AND_MOVE(SemanticContextCapabilityResources);

protected:
  SemanticContextCapabilityResources() = default;
};

/// \brief Session lifetime anchor for revision-local semantic capabilities.
///
/// The compiler session owns one arena and places its semantic-context issuer,
/// identity registries, and semantic type store behind that lifetime boundary.
/// The query runtime deliberately treats those resources as opaque.
class SemanticContextCapabilityArena final : public zc::AtomicRefcounted {
public:
  SemanticContextCapabilityArena();
  explicit SemanticContextCapabilityArena(zc::Own<SemanticContextCapabilityResources>&& resources);
  ~SemanticContextCapabilityArena() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(SemanticContextCapabilityArena);

  ZC_NODISCARD bool hasResources() const noexcept;

private:
  ZC_NODISCARD const SemanticContextCapabilityResources& resources() const ZC_LIFETIMEBOUND;

  struct Impl;
  zc::Own<Impl> impl;

  friend class SnapshotCapabilityArena;
};

/// \brief Immutable lifetime anchor created once for one query snapshot.
class SnapshotCapabilityArena final : public zc::AtomicRefcounted {
public:
  SnapshotCapabilityArena(DatabaseRevision revision,
                          zc::Arc<SemanticContextCapabilityArena>&& context);
  ~SnapshotCapabilityArena() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(SnapshotCapabilityArena);

  ZC_NODISCARD DatabaseRevision revision() const noexcept;

private:
  ZC_NODISCARD const SemanticContextCapabilityResources& resources() const ZC_LIFETIMEBOUND;

  struct Impl;
  zc::Own<Impl> impl;

  friend class QueryContext;
};

/// \brief Type-erased immutable owner of one revision-local capability generation.
class RevisionLocalCapabilityMemoBase : public zc::AtomicRefcounted {
public:
  ~RevisionLocalCapabilityMemoBase() noexcept(false) override;
  ZC_DISALLOW_COPY_AND_MOVE(RevisionLocalCapabilityMemoBase);

private:
  RevisionLocalCapabilityMemoBase(
      QueryDatabaseIdentity&& database, CanonicalQueryKey&& key, DatabaseRevision revision,
      zc::Arc<SnapshotCapabilityArena>&& arena,
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies,
      zc::Array<uint8_t>&& stableWitness);

private:
  ZC_NODISCARD const QueryDatabaseIdentity& database() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const CanonicalQueryKey& key() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const SnapshotCapabilityArena& arena() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const zc::Arc<RevisionLocalCapabilityMemoBase>> retainedDependencies()
      const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> stableWitness() const ZC_LIFETIMEBOUND;

  struct Impl;
  zc::Own<Impl> impl;

  friend class _query_detail::CapabilityMemoBuilderBase;
  friend class _query_detail::QueryRequestResultAccess;
  template <typename Capability>
  friend class RevisionLocalCapabilityMemo;
  template <typename Spec>
  friend class CapabilityResultDecoder;
  template <typename Capability>
  friend class QueryCapabilityLease;
};

/// \brief Immutable memo generation owning exactly one move-only capability.
template <typename Capability>
class RevisionLocalCapabilityMemo final : public RevisionLocalCapabilityMemoBase {
public:
  /// \brief Constructs one memo from query-runtime-only authorities.
  ///
  /// The parameter types remain nonconstructible outside the query runtime.
  RevisionLocalCapabilityMemo(
      QueryDatabaseIdentity&& database, CanonicalQueryKey&& key, DatabaseRevision revision,
      zc::Arc<SnapshotCapabilityArena>&& arena,
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies,
      zc::Array<uint8_t>&& stableWitness, zc::Own<Capability>&& capability)
      : RevisionLocalCapabilityMemoBase(zc::mv(database), zc::mv(key), revision, zc::mv(arena),
                                        zc::mv(retainedDependencies), zc::mv(stableWitness)),
        capabilityField(zc::mv(capability)) {}

private:
  ZC_NODISCARD const Capability& capability() const ZC_LIFETIMEBOUND { return *capabilityField; }

  zc::Own<Capability> capabilityField;

  template <typename>
  friend class _query_detail::CapabilityMemoBuilder;
  template <typename Spec>
  friend class CapabilityResultDecoder;
  template <typename>
  friend class QueryCapabilityLease;
};

/// \brief Retainable strong lease to one immutable capability memo generation.
template <typename Capability>
class QueryCapabilityLease final {
public:
  using StoredCapability = zc::RemoveConst<Capability>;

  QueryCapabilityLease(QueryCapabilityLease&&) noexcept = default;
  QueryCapabilityLease& operator=(QueryCapabilityLease&&) noexcept = default;
  ZC_DISALLOW_COPY(QueryCapabilityLease);

  /// \brief Retains the same memo generation without copying its capability.
  ZC_NODISCARD QueryCapabilityLease retain() const {
    return QueryCapabilityLease(memoField.addRef());
  }
  ZC_NODISCARD const StoredCapability& capability() const ZC_LIFETIMEBOUND {
    return memoField->capability();
  }
  ZC_NODISCARD const StoredCapability& operator*() const ZC_LIFETIMEBOUND { return capability(); }
  ZC_NODISCARD DatabaseRevision revision() const noexcept { return memoField->revision(); }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> stableWitness() const ZC_LIFETIMEBOUND {
    return memoField->stableWitness();
  }
  ZC_NODISCARD const CanonicalQueryKey& key() const ZC_LIFETIMEBOUND { return memoField->key(); }
  ZC_NODISCARD size_t retainedDependencyCount() const noexcept {
    return memoField->retainedDependencies().size();
  }
  ZC_NODISCARD DatabaseRevision arenaRevision() const noexcept {
    return memoField->arena().revision();
  }

private:
  explicit QueryCapabilityLease(
      zc::Arc<RevisionLocalCapabilityMemo<StoredCapability>>&& memo) noexcept
      : memoField(zc::mv(memo)) {}

  zc::Arc<RevisionLocalCapabilityMemo<StoredCapability>> memoField;

  template <typename Spec>
  friend class CapabilityResultDecoder;
};

/// \brief Runtime-only alternatives returned by a capability demand.
enum class CapabilityDemandResultKind : uint8_t {
  Published = 0x01,
  SourceRejected = 0x02,
  KeyRejected = 0x03,
  RuntimeRejected = 0x04
};

/// \brief Runtime failures that never become reusable semantic values.
enum class QueryRuntimeFailure : uint8_t {
  UnregisteredKind = 0x01,
  InvalidKeyEncoding = 0x02,
  MissingInput = 0x03,
  ProviderRejected = 0x04,
  VerifierRejected = 0x05,
  Cycle = 0x06,
  Cancelled = 0x07,
  FingerprintCollision = 0x08,
  InvariantViolation = 0x09,
  FinalSealRequired = 0x0a,
  FinalSealMismatch = 0x0b,
  AllocationFailure = 0x0c
};

/// \brief Canonical descriptor rejection alternatives carried by the evaluator.
enum class CapabilityFailureKind : uint8_t { SourceRejected = 0x01, KeyRejected = 0x02 };

/// \brief Independent verification outcome for one descriptor-owned rejection.
enum class CapabilityRejectionCheck : uint8_t { Verified = 0x01, Rejected = 0x02 };

/// \brief Verified type-erased capability rejection passed through the evaluator.
class CapabilityFailureEnvelope final {
public:
  CapabilityFailureEnvelope(CapabilityFailureEnvelope&&) noexcept = default;
  CapabilityFailureEnvelope& operator=(CapabilityFailureEnvelope&&) noexcept = default;
  ZC_DISALLOW_COPY(CapabilityFailureEnvelope);

private:
  CapabilityFailureEnvelope(zc::String&& descriptorDomain, CapabilityFailureKind kind,
                            zc::Array<uint8_t>&& canonicalPayload);

  ZC_NODISCARD static zc::Maybe<CapabilityFailureEnvelope> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Maybe<CapabilityFailureEnvelope> decodeCanonical() const {
    return decodeCanonical(canonicalBytesField.asPtr());
  }

  ZC_NODISCARD zc::StringPtr descriptorDomain() const ZC_LIFETIMEBOUND {
    return descriptorDomainField;
  }
  ZC_NODISCARD CapabilityFailureKind kind() const noexcept { return kindField; }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalPayload() const ZC_LIFETIMEBOUND {
    return canonicalPayloadField.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalBytes() const ZC_LIFETIMEBOUND {
    return canonicalBytesField.asPtr();
  }

  zc::String descriptorDomainField;
  CapabilityFailureKind kindField;
  zc::Array<uint8_t> canonicalPayloadField;
  zc::Array<uint8_t> canonicalBytesField;

  friend class QueryDatabase;
  friend class _query_detail::QueryRequestResultAccess;
  template <typename Spec>
  friend class CapabilityResultDecoder;
};

/// \brief Closed descriptor-owned list of legal capability rejection alternatives.
template <typename... Alternatives>
class CapabilityFailureList final {};

/// \brief Descriptor-owned source rejection carrying one canonical diagnostic type.
template <typename Diagnostic>
class SourceRejection final {};

/// \brief Descriptor-owned key rejection carrying one canonical key-failure type.
template <typename KeyFailure>
class KeyRejection final {};

template <typename Descriptor, typename Alternative>
class CapabilityFailureContract;

/// \brief Move-only nonempty sequence admitted by one capability failure contract.
template <typename T>
class CanonicalNonEmptySequence final {
public:
  CanonicalNonEmptySequence(CanonicalNonEmptySequence&&) noexcept = default;
  CanonicalNonEmptySequence& operator=(CanonicalNonEmptySequence&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalNonEmptySequence);

  ZC_NODISCARD zc::ArrayPtr<const T> values() const ZC_LIFETIMEBOUND { return valuesField.asPtr(); }

private:
  explicit CanonicalNonEmptySequence(zc::Vector<T>&& values) : valuesField(zc::mv(values)) {
    ZC_IREQUIRE(valuesField.size() != 0,
                "canonical capability rejection sequence must not be empty");
  }

  zc::Vector<T> valuesField;

  template <typename Descriptor, typename Alternative>
  friend class CapabilityFailureContract;
};

namespace _query_detail {

template <typename Alternative>
struct CapabilityFailureAlternative final {
  static constexpr bool supported = false;
  static constexpr bool source = false;
  static constexpr bool key = false;
};

template <typename Diagnostic>
struct CapabilityFailureAlternative<SourceRejection<Diagnostic>> final {
  using Payload = Diagnostic;
  static constexpr bool supported = true;
  static constexpr bool source = true;
  static constexpr bool key = false;
};

template <typename KeyFailure>
struct CapabilityFailureAlternative<KeyRejection<KeyFailure>> final {
  using Payload = KeyFailure;
  static constexpr bool supported = true;
  static constexpr bool source = false;
  static constexpr bool key = true;
};

template <typename Needle, typename List>
struct CapabilityFailureListContains;

template <typename Needle, typename... Alternatives>
struct CapabilityFailureListContains<Needle, CapabilityFailureList<Alternatives...>> final {
  static constexpr bool value = (false || ... || zc::isSameType<Needle, Alternatives>());
};

template <typename List>
struct CapabilityFailureListShape;

template <typename... Alternatives>
struct CapabilityFailureListShape<CapabilityFailureList<Alternatives...>> final {
  static constexpr size_t sourceCount =
      (size_t{0} + ... + (CapabilityFailureAlternative<Alternatives>::source ? 1 : 0));
  static constexpr size_t keyCount =
      (size_t{0} + ... + (CapabilityFailureAlternative<Alternatives>::key ? 1 : 0));
  static constexpr bool supported =
      (true && ... && CapabilityFailureAlternative<Alternatives>::supported);
};

template <typename... Alternatives>
struct SourceFailurePayload {
  using Type = void;
};

template <typename Diagnostic, typename... Alternatives>
struct SourceFailurePayload<SourceRejection<Diagnostic>, Alternatives...> {
  using Type = Diagnostic;
};

template <typename Alternative, typename... Alternatives>
struct SourceFailurePayload<Alternative, Alternatives...> : SourceFailurePayload<Alternatives...> {
};

template <typename... Alternatives>
struct KeyFailurePayload {
  using Type = void;
};

template <typename KeyFailure, typename... Alternatives>
struct KeyFailurePayload<KeyRejection<KeyFailure>, Alternatives...> {
  using Type = KeyFailure;
};

template <typename Alternative, typename... Alternatives>
struct KeyFailurePayload<Alternative, Alternatives...> : KeyFailurePayload<Alternatives...> {};

template <typename List>
struct CapabilityFailurePayloads;

template <typename... Alternatives>
struct CapabilityFailurePayloads<CapabilityFailureList<Alternatives...>> final {
  using Source = typename SourceFailurePayload<Alternatives...>::Type;
  using Key = typename KeyFailurePayload<Alternatives...>::Type;
};

template <typename Descriptor>
class PublishedCapabilityDemand final {
public:
  explicit PublishedCapabilityDemand(
      QueryCapabilityLease<const typename Descriptor::Capability>&& lease) noexcept
      : leaseField(zc::mv(lease)) {}

  QueryCapabilityLease<const typename Descriptor::Capability> leaseField;
};

template <typename Diagnostic>
class SourceRejectedCapabilityDemand final {
public:
  explicit SourceRejectedCapabilityDemand(
      CanonicalNonEmptySequence<Diagnostic>&& diagnostics) noexcept
      : diagnosticsField(zc::mv(diagnostics)) {}

  CanonicalNonEmptySequence<Diagnostic> diagnosticsField;
};

template <typename KeyFailure>
class KeyRejectedCapabilityDemand final {
public:
  explicit KeyRejectedCapabilityDemand(KeyFailure&& failure) noexcept
      : failureField(zc::mv(failure)) {}

  KeyFailure failureField;
};

class RuntimeRejectedCapabilityDemand final {
public:
  explicit RuntimeRejectedCapabilityDemand(QueryRuntimeFailure failure) noexcept
      : failureField(failure) {}

  QueryRuntimeFailure failureField;
};

template <typename Alternative>
struct CapabilityDemandAlternative;

template <typename Diagnostic>
struct CapabilityDemandAlternative<SourceRejection<Diagnostic>> final {
  using Type = SourceRejectedCapabilityDemand<Diagnostic>;
};

template <typename KeyFailure>
struct CapabilityDemandAlternative<KeyRejection<KeyFailure>> final {
  using Type = KeyRejectedCapabilityDemand<KeyFailure>;
};

template <typename Descriptor, typename List>
struct CapabilityDemandStorage;

template <typename Descriptor, typename... Alternatives>
struct CapabilityDemandStorage<Descriptor, CapabilityFailureList<Alternatives...>> final {
  static_assert(CapabilityFailureListShape<CapabilityFailureList<Alternatives...>>::supported,
                "capability descriptor lists an unsupported failure alternative");
  static_assert(CapabilityFailureListShape<CapabilityFailureList<Alternatives...>>::sourceCount <=
                    1,
                "capability descriptor lists more than one source rejection");
  static_assert(CapabilityFailureListShape<CapabilityFailureList<Alternatives...>>::keyCount <= 1,
                "capability descriptor lists more than one key rejection");

  using Type = zc::OneOf<PublishedCapabilityDemand<Descriptor>,
                         typename CapabilityDemandAlternative<Alternatives>::Type...,
                         RuntimeRejectedCapabilityDemand>;
};

}  // namespace _query_detail

/// \brief Move-only caller result of demanding one retained capability.
template <typename Descriptor>
class CapabilityDemandResult final {
public:
  using FailureAlternatives = typename Descriptor::FailureAlternatives;

  CapabilityDemandResult(CapabilityDemandResult&&) noexcept = default;
  CapabilityDemandResult& operator=(CapabilityDemandResult&&) noexcept = default;
  ZC_DISALLOW_COPY(CapabilityDemandResult);

  ZC_NODISCARD static CapabilityDemandResult published(
      QueryCapabilityLease<const typename Descriptor::Capability>&& lease) {
    return CapabilityDemandResult(
        CapabilityDemandResultKind::Published,
        _query_detail::PublishedCapabilityDemand<Descriptor>(zc::mv(lease)));
  }

  template <typename Diagnostic>
    requires(_query_detail::CapabilityFailureListContains<SourceRejection<Diagnostic>,
                                                          FailureAlternatives>::value)
  ZC_NODISCARD static CapabilityDemandResult sourceRejected(
      CanonicalNonEmptySequence<Diagnostic>&& diagnostics) {
    return CapabilityDemandResult(
        CapabilityDemandResultKind::SourceRejected,
        _query_detail::SourceRejectedCapabilityDemand<Diagnostic>(zc::mv(diagnostics)));
  }

  template <typename KeyFailure>
    requires(_query_detail::CapabilityFailureListContains<KeyRejection<KeyFailure>,
                                                          FailureAlternatives>::value)
  ZC_NODISCARD static CapabilityDemandResult keyRejected(KeyFailure&& failure) {
    return CapabilityDemandResult(
        CapabilityDemandResultKind::KeyRejected,
        _query_detail::KeyRejectedCapabilityDemand<KeyFailure>(zc::mv(failure)));
  }

  ZC_NODISCARD static CapabilityDemandResult runtimeRejected(QueryRuntimeFailure failure) {
    return CapabilityDemandResult(CapabilityDemandResultKind::RuntimeRejected,
                                  _query_detail::RuntimeRejectedCapabilityDemand(failure));
  }

  ZC_NODISCARD CapabilityDemandResultKind kind() const noexcept { return kindField; }
  ZC_NODISCARD bool isPublished() const noexcept {
    return kindField == CapabilityDemandResultKind::Published;
  }
  template <typename Alternatives = FailureAlternatives>
    requires(_query_detail::CapabilityFailureListShape<Alternatives>::sourceCount == 1)
  ZC_NODISCARD bool isSourceRejected() const noexcept {
    return kindField == CapabilityDemandResultKind::SourceRejected;
  }
  template <typename Alternatives = FailureAlternatives>
    requires(_query_detail::CapabilityFailureListShape<Alternatives>::keyCount == 1)
  ZC_NODISCARD bool isKeyRejected() const noexcept {
    return kindField == CapabilityDemandResultKind::KeyRejected;
  }
  ZC_NODISCARD bool isRuntimeRejected() const noexcept {
    return kindField == CapabilityDemandResultKind::RuntimeRejected;
  }

  ZC_NODISCARD const QueryCapabilityLease<const typename Descriptor::Capability>& lease() const
      ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isPublished(), "capability demand did not publish a lease");
    return storageField.template get<_query_detail::PublishedCapabilityDemand<Descriptor>>()
        .leaseField;
  }
  ZC_NODISCARD QueryCapabilityLease<const typename Descriptor::Capability> takeLease() && {
    ZC_IREQUIRE(isPublished(), "capability demand did not publish a lease");
    return zc::mv(storageField)
        .template get<_query_detail::PublishedCapabilityDemand<Descriptor>>()
        .leaseField;
  }

  template <typename Alternatives = FailureAlternatives>
    requires(_query_detail::CapabilityFailureListShape<Alternatives>::sourceCount == 1)
  ZC_NODISCARD const auto& diagnostics() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isSourceRejected(), "capability demand has no source diagnostics");
    using Diagnostic = typename _query_detail::CapabilityFailurePayloads<Alternatives>::Source;
    return storageField.template get<_query_detail::SourceRejectedCapabilityDemand<Diagnostic>>()
        .diagnosticsField;
  }

  template <typename Alternatives = FailureAlternatives>
    requires(_query_detail::CapabilityFailureListShape<Alternatives>::keyCount == 1)
  ZC_NODISCARD const auto& keyFailure() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isKeyRejected(), "capability demand has no key failure");
    using KeyFailure = typename _query_detail::CapabilityFailurePayloads<Alternatives>::Key;
    return storageField.template get<_query_detail::KeyRejectedCapabilityDemand<KeyFailure>>()
        .failureField;
  }

  ZC_NODISCARD QueryRuntimeFailure runtimeFailure() const noexcept {
    ZC_IREQUIRE(isRuntimeRejected(), "capability demand has no runtime failure");
    return storageField.template get<_query_detail::RuntimeRejectedCapabilityDemand>().failureField;
  }

private:
  using Storage =
      typename _query_detail::CapabilityDemandStorage<Descriptor, FailureAlternatives>::Type;

  template <typename Alternative>
  CapabilityDemandResult(CapabilityDemandResultKind kind, Alternative&& alternative) noexcept
      : kindField(kind), storageField(zc::mv(alternative)) {}

  CapabilityDemandResultKind kindField;
  Storage storageField;
};

/// \brief Universal result of a query demand.
class QueryRequestResult final {
public:
  QueryRequestResult(QueryRequestResult&&) noexcept = default;
  QueryRequestResult& operator=(QueryRequestResult&&) noexcept = default;
  ZC_DISALLOW_COPY(QueryRequestResult);

private:
  using Storage = zc::OneOf<QueryValue, zc::Arc<RevisionLocalCapabilityMemoBase>,
                            CapabilityFailureEnvelope, QueryRuntimeFailure>;

  explicit QueryRequestResult(QueryValue&& value) : storageField(zc::mv(value)) {}
  explicit QueryRequestResult(zc::Arc<RevisionLocalCapabilityMemoBase>&& capabilityMemo)
      : storageField(zc::mv(capabilityMemo)) {
    ZC_IREQUIRE(storageField.template get<zc::Arc<RevisionLocalCapabilityMemoBase>>() != nullptr,
                "published capability result requires a memo");
  }
  explicit QueryRequestResult(CapabilityFailureEnvelope&& rejection)
      : storageField(zc::mv(rejection)) {}
  explicit QueryRequestResult(QueryRuntimeFailure failure) : storageField(failure) {}

  Storage storageField;

  friend class QueryDatabase;
  friend class _query_detail::QueryRequestResultAccess;
  template <typename Spec>
  friend class CapabilityResultDecoder;
  friend class test::QueryRuntimeTestAccess;
};

/// \brief One actual dependency read and its observed semantic metadata.
class DependencyRecord final {
public:
  DependencyRecord(CanonicalQueryKey&& key, DatabaseRevision changedAt, Durability durability,
                   zc::Maybe<InputProbeObservation> inputProbeObservation = zc::none) noexcept;
  DependencyRecord(DependencyRecord&&) noexcept = default;
  DependencyRecord& operator=(DependencyRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(DependencyRecord);

  ZC_NODISCARD DependencyRecord clone() const;
  ZC_NODISCARD const CanonicalQueryKey& key() const ZC_LIFETIMEBOUND { return keyField; }
  ZC_NODISCARD DatabaseRevision changedAt() const noexcept { return changedAtField; }
  ZC_NODISCARD Durability durability() const noexcept { return durabilityField; }
  ZC_NODISCARD zc::Maybe<InputProbeObservation> inputProbeObservation() const noexcept {
    return inputProbeObservationField;
  }
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> stableWitness() const noexcept;

  /// \brief Constructs a tracked dependency on one revision-local capability.
  ZC_NODISCARD static DependencyRecord revisionLocalCapability(
      CanonicalQueryKey&& key, DatabaseRevision changedAt, Durability durability,
      zc::ArrayPtr<const uint8_t> stableWitness);

private:
  CanonicalQueryKey keyField;
  DatabaseRevision changedAtField;
  Durability durabilityField;
  zc::Maybe<InputProbeObservation> inputProbeObservationField;
  zc::Maybe<zc::Array<uint8_t>> stableWitnessField;
};

/// \brief Deterministic sequential or explicitly parallel dependency execution group.
class DependencyGroup final {
public:
  enum class Kind : uint8_t { Sequential = 0, Parallel = 1 };

  DependencyGroup(DependencyGroup&&) noexcept = default;
  DependencyGroup& operator=(DependencyGroup&&) noexcept = default;
  ZC_DISALLOW_COPY(DependencyGroup);

  ZC_NODISCARD static DependencyGroup sequential(DependencyRecord&& dependency);
  ZC_NODISCARD static DependencyGroup parallel(zc::Vector<DependencyRecord>&& dependencies);

  ZC_NODISCARD DependencyGroup clone() const;
  ZC_NODISCARD Kind kind() const noexcept { return kindField; }
  ZC_NODISCARD zc::ArrayPtr<const DependencyRecord> dependencies() const ZC_LIFETIMEBOUND {
    return dependencyFields.asPtr();
  }

private:
  DependencyGroup(Kind kind, zc::Vector<DependencyRecord>&& dependencies) noexcept;

  Kind kindField;
  zc::Vector<DependencyRecord> dependencyFields;
};

/// \brief Immutable validation metadata committed with a memo value.
class MemoMetadata final {
public:
  constexpr MemoMetadata() noexcept = default;
  constexpr MemoMetadata(DatabaseRevision verifiedAt, DatabaseRevision changedAt,
                         Durability minimumDurability) noexcept
      : verifiedAtField(verifiedAt),
        changedAtField(changedAt),
        minimumDurabilityField(minimumDurability) {}

  ZC_NODISCARD constexpr DatabaseRevision verifiedAt() const noexcept { return verifiedAtField; }
  ZC_NODISCARD constexpr DatabaseRevision changedAt() const noexcept { return changedAtField; }
  ZC_NODISCARD constexpr Durability minimumDurability() const noexcept {
    return minimumDurabilityField;
  }

private:
  DatabaseRevision verifiedAtField;
  DatabaseRevision changedAtField;
  Durability minimumDurabilityField = Durability::Frozen;
};

/// \brief Deterministic demand, coalescing, and retention telemetry classification.
enum class QueryEventKind : uint8_t {
  Executed = 0,
  GreenReused = 1,
  RecomputedEqual = 2,
  RecomputedChanged = 3,
  DurabilityDecreased = 4,
  Cancelled = 5,
  Cycle = 6,
  VerifierRejected = 7,
  RuntimeFailed = 8,
  SingleFlightJoined = 9,
  ValueEvicted = 10
};

/// \brief Stable event emitted by the in-memory runtime for tests and telemetry.
class QueryEvent final {
public:
  QueryEvent(DatabaseRevision revision, CanonicalQueryKey&& key, QueryEventKind kind) noexcept;
  QueryEvent(QueryEvent&&) noexcept = default;
  QueryEvent& operator=(QueryEvent&&) noexcept = default;
  ZC_DISALLOW_COPY(QueryEvent);

  ZC_NODISCARD QueryEvent clone() const;
  ZC_NODISCARD DatabaseRevision revision() const noexcept { return revisionField; }
  ZC_NODISCARD const CanonicalQueryKey& key() const ZC_LIFETIMEBOUND { return keyField; }
  ZC_NODISCARD QueryEventKind kind() const noexcept { return kindField; }

private:
  DatabaseRevision revisionField;
  CanonicalQueryKey keyField;
  QueryEventKind kindField;
};

}  // namespace zomlang::compiler::query
