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
#include "zc/core/refcount.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::binder {
class BinderKeyFailure;
}

namespace zomlang::compiler::diagnostics {
struct DiagnosticFact;
}

namespace zomlang::compiler::identity {
class Sha256Digest;
}

namespace zomlang::compiler::query {

class QueryDatabase;

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

/// \brief Process-local identity of one query database instance.
class QueryDatabaseIdentity final {
public:
  constexpr QueryDatabaseIdentity() noexcept = default;

  ZC_NODISCARD constexpr uint64_t value() const noexcept { return valueField; }
  constexpr bool operator==(QueryDatabaseIdentity other) const noexcept {
    return valueField == other.valueField;
  }
  constexpr bool operator!=(QueryDatabaseIdentity other) const noexcept {
    return !(*this == other);
  }

private:
  explicit constexpr QueryDatabaseIdentity(uint64_t value) noexcept : valueField(value) {}

  uint64_t valueField = 0;

  friend class QueryDatabase;
};

/// \brief Move-only proof that one exact database snapshot has final immutable inputs.
template <typename ContextRoots, typename FinalWitness = identity::Sha256Digest>
class FinalSnapshotSeal final {
public:
  FinalSnapshotSeal(FinalSnapshotSeal&&) noexcept = default;
  FinalSnapshotSeal& operator=(FinalSnapshotSeal&&) noexcept = default;
  ZC_DISALLOW_COPY(FinalSnapshotSeal);

  ZC_NODISCARD QueryDatabaseIdentity database() const noexcept { return databaseField; }
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
  FinalSnapshotSeal(QueryDatabaseIdentity database, DatabaseRevision revision,
                    ContextRoots&& contextRoots, const FinalWitness& finalWitness) noexcept
      : databaseField(database),
        revisionField(revision),
        contextRootsField(zc::mv(contextRoots)),
        finalWitnessField(finalWitness) {}

  QueryDatabaseIdentity databaseField;
  DatabaseRevision revisionField;
  ContextRoots contextRootsField;
  FinalWitness finalWitnessField;

  friend class QueryDatabase;
};

/// \brief Closed validation durability lattice ordered from mutable to immutable.
enum class Durability : uint8_t { Low = 0, Medium = 1, High = 2, Frozen = 3 };

/// \brief Cross-revision reuse contract of one query kind.
enum class ReuseClass : uint8_t { RevisionLocal = 0, Semantic = 1, Persisted = 2 };

/// \brief Retention policy for the complete equality witness.
enum class RetentionClass : uint8_t { Retained = 0, Evictable = 1 };

/// \brief Stable in-process index assigned to one registered query kind.
class QueryKindId final {
public:
  constexpr QueryKindId() noexcept = default;
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
  uint32_t valueField = 0;
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

  ZC_NODISCARD const CanonicalQueryKey& key() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const SnapshotCapabilityArena& arena() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const zc::Arc<RevisionLocalCapabilityMemoBase>> retainedDependencies()
      const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> stableWitness() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::StringPtr capabilityTypeIdentity() const ZC_LIFETIMEBOUND;

protected:
  RevisionLocalCapabilityMemoBase(
      CanonicalQueryKey&& key, DatabaseRevision revision, zc::Arc<SnapshotCapabilityArena>&& arena,
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies,
      zc::Array<uint8_t>&& stableWitness, zc::StringPtr capabilityTypeIdentity);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

namespace _query_detail {

template <typename Capability>
zc::StringPtr capabilityTypeIdentity() {
#if defined(__clang__) || defined(__GNUC__)
  return zc::StringPtr(__PRETTY_FUNCTION__);
#elif defined(_MSC_VER)
  return zc::StringPtr(__FUNCSIG__);
#else
#error "A compiler-provided function signature is required for capability type identity"
#endif
}

}  // namespace _query_detail

template <typename Spec>
class CapabilityResultDecoder;

/// \brief Immutable memo generation owning exactly one move-only capability.
template <typename Capability>
class RevisionLocalCapabilityMemo final : public RevisionLocalCapabilityMemoBase {
public:
  RevisionLocalCapabilityMemo(
      CanonicalQueryKey&& key, DatabaseRevision revision, zc::Arc<SnapshotCapabilityArena>&& arena,
      zc::Vector<zc::Arc<RevisionLocalCapabilityMemoBase>>&& retainedDependencies,
      zc::Array<uint8_t>&& stableWitness, zc::Own<Capability>&& capability)
      : RevisionLocalCapabilityMemoBase(zc::mv(key), revision, zc::mv(arena),
                                        zc::mv(retainedDependencies), zc::mv(stableWitness),
                                        _query_detail::capabilityTypeIdentity<Capability>()),
        capabilityField(zc::mv(capability)) {}

  ZC_NODISCARD const Capability& capability() const ZC_LIFETIMEBOUND { return *capabilityField; }

private:
  zc::Own<Capability> capabilityField;
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
  UnregisteredKind = 0,
  InvalidKeyEncoding = 1,
  MissingInput = 2,
  ProviderRejected = 3,
  VerifierRejected = 4,
  Cycle = 5,
  Cancelled = 6,
  FingerprintCollision = 7,
  InvariantViolation = 8
};

/// \brief Move-only caller result of demanding one retained capability.
///
/// Diagnostic and key-failure payloads remain dependent so the query layer
/// does not acquire dependencies on diagnostics or Binder. Callers instantiate
/// this result only where those concrete payload types are complete.
template <typename Capability, typename Diagnostic = diagnostics::DiagnosticFact,
          typename KeyFailure = binder::BinderKeyFailure>
class CapabilityDemandResult final {
public:
  CapabilityDemandResult(CapabilityDemandResult&&) noexcept = default;
  CapabilityDemandResult& operator=(CapabilityDemandResult&&) noexcept = default;
  ZC_DISALLOW_COPY(CapabilityDemandResult);

  ZC_NODISCARD static CapabilityDemandResult published(
      QueryCapabilityLease<const Capability>&& lease) {
    zc::Maybe<QueryCapabilityLease<const Capability>> retainedLease(zc::mv(lease));
    zc::Maybe<KeyFailure> noKeyFailure;
    return CapabilityDemandResult(CapabilityDemandResultKind::Published, zc::mv(retainedLease),
                                  zc::Vector<Diagnostic>(), zc::mv(noKeyFailure),
                                  QueryRuntimeFailure::InvariantViolation);
  }

  ZC_NODISCARD static CapabilityDemandResult sourceRejected(
      zc::Vector<Diagnostic>&& canonicalDiagnostics) {
    ZC_IREQUIRE(canonicalDiagnostics.size() != 0,
                "source-rejected capability demand has no diagnostics");
    zc::Maybe<QueryCapabilityLease<const Capability>> noLease;
    zc::Maybe<KeyFailure> noKeyFailure;
    return CapabilityDemandResult(CapabilityDemandResultKind::SourceRejected, zc::mv(noLease),
                                  zc::mv(canonicalDiagnostics), zc::mv(noKeyFailure),
                                  QueryRuntimeFailure::InvariantViolation);
  }

  ZC_NODISCARD static CapabilityDemandResult keyRejected(KeyFailure&& failure) {
    zc::Maybe<QueryCapabilityLease<const Capability>> noLease;
    zc::Maybe<KeyFailure> retainedFailure(zc::mv(failure));
    return CapabilityDemandResult(CapabilityDemandResultKind::KeyRejected, zc::mv(noLease),
                                  zc::Vector<Diagnostic>(), zc::mv(retainedFailure),
                                  QueryRuntimeFailure::InvariantViolation);
  }

  ZC_NODISCARD static CapabilityDemandResult runtimeRejected(QueryRuntimeFailure failure) {
    zc::Maybe<QueryCapabilityLease<const Capability>> noLease;
    zc::Maybe<KeyFailure> noKeyFailure;
    return CapabilityDemandResult(CapabilityDemandResultKind::RuntimeRejected, zc::mv(noLease),
                                  zc::Vector<Diagnostic>(), zc::mv(noKeyFailure), failure);
  }

  ZC_NODISCARD CapabilityDemandResultKind kind() const noexcept { return kindField; }
  ZC_NODISCARD bool isPublished() const noexcept {
    return kindField == CapabilityDemandResultKind::Published;
  }
  ZC_NODISCARD bool isSourceRejected() const noexcept {
    return kindField == CapabilityDemandResultKind::SourceRejected;
  }
  ZC_NODISCARD bool isKeyRejected() const noexcept {
    return kindField == CapabilityDemandResultKind::KeyRejected;
  }
  ZC_NODISCARD bool isRuntimeRejected() const noexcept {
    return kindField == CapabilityDemandResultKind::RuntimeRejected;
  }

  ZC_NODISCARD const QueryCapabilityLease<const Capability>& lease() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isPublished(), "capability demand did not publish a lease");
    return ZC_ASSERT_NONNULL(leaseField);
  }
  ZC_NODISCARD QueryCapabilityLease<const Capability> takeLease() && {
    ZC_IREQUIRE(isPublished(), "capability demand did not publish a lease");
    return zc::mv(ZC_ASSERT_NONNULL(leaseField));
  }
  ZC_NODISCARD zc::ArrayPtr<const Diagnostic> diagnostics() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isSourceRejected(), "capability demand has no source diagnostics");
    return diagnosticFields.asPtr();
  }
  ZC_NODISCARD const KeyFailure& keyFailure() const ZC_LIFETIMEBOUND {
    ZC_IREQUIRE(isKeyRejected(), "capability demand has no key failure");
    return ZC_ASSERT_NONNULL(keyFailureField);
  }
  ZC_NODISCARD QueryRuntimeFailure runtimeFailure() const noexcept {
    ZC_IREQUIRE(isRuntimeRejected(), "capability demand has no runtime failure");
    return runtimeFailureField;
  }

private:
  CapabilityDemandResult(CapabilityDemandResultKind kind,
                         zc::Maybe<QueryCapabilityLease<const Capability>>&& lease,
                         zc::Vector<Diagnostic>&& diagnostics, zc::Maybe<KeyFailure>&& keyFailure,
                         QueryRuntimeFailure runtimeFailure) noexcept
      : kindField(kind),
        leaseField(zc::mv(lease)),
        diagnosticFields(zc::mv(diagnostics)),
        keyFailureField(zc::mv(keyFailure)),
        runtimeFailureField(runtimeFailure) {}

  CapabilityDemandResultKind kindField;
  zc::Maybe<QueryCapabilityLease<const Capability>> leaseField;
  zc::Vector<Diagnostic> diagnosticFields;
  zc::Maybe<KeyFailure> keyFailureField;
  QueryRuntimeFailure runtimeFailureField;
};

/// \brief Universal result of a query demand.
class QueryRequestResult final {
public:
  QueryRequestResult(QueryRequestResult&&) noexcept = default;
  QueryRequestResult& operator=(QueryRequestResult&&) noexcept = default;
  ZC_DISALLOW_COPY(QueryRequestResult);

  ZC_NODISCARD static QueryRequestResult completed(QueryValue&& value);
  ZC_NODISCARD static QueryRequestResult completed(
      zc::Arc<RevisionLocalCapabilityMemoBase>&& capabilityMemo);
  ZC_NODISCARD static QueryRequestResult failed(QueryRuntimeFailure failure);

  ZC_NODISCARD QueryRequestResult clone() const;
  ZC_NODISCARD bool isCompleted() const noexcept {
    return valueField != zc::none || capabilityMemoField != nullptr;
  }
  ZC_NODISCARD bool isCapability() const noexcept { return capabilityMemoField != nullptr; }
  ZC_NODISCARD const QueryValue& value() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const RevisionLocalCapabilityMemoBase& capabilityMemo() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Arc<RevisionLocalCapabilityMemoBase> capabilityMemoArc() const;
  ZC_NODISCARD zc::Arc<RevisionLocalCapabilityMemoBase> takeCapabilityMemo();
  ZC_NODISCARD QueryRuntimeFailure failure() const noexcept { return failureField; }

private:
  QueryRequestResult(zc::Maybe<QueryValue>&& value,
                     zc::Arc<RevisionLocalCapabilityMemoBase>&& capabilityMemo,
                     QueryRuntimeFailure failure) noexcept;

  zc::Maybe<QueryValue> valueField;
  zc::Arc<RevisionLocalCapabilityMemoBase> capabilityMemoField;
  QueryRuntimeFailure failureField = QueryRuntimeFailure::InvariantViolation;
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
