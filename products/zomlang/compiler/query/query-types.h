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
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::query {

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

/// \brief Universal result of a query demand.
class QueryRequestResult final {
public:
  QueryRequestResult(QueryRequestResult&&) noexcept = default;
  QueryRequestResult& operator=(QueryRequestResult&&) noexcept = default;
  ZC_DISALLOW_COPY(QueryRequestResult);

  ZC_NODISCARD static QueryRequestResult completed(QueryValue&& value);
  ZC_NODISCARD static QueryRequestResult failed(QueryRuntimeFailure failure);

  ZC_NODISCARD QueryRequestResult clone() const;
  ZC_NODISCARD bool isCompleted() const noexcept { return valueField != zc::none; }
  ZC_NODISCARD const QueryValue& value() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD QueryRuntimeFailure failure() const noexcept { return failureField; }

private:
  QueryRequestResult(zc::Maybe<QueryValue>&& value, QueryRuntimeFailure failure) noexcept;

  zc::Maybe<QueryValue> valueField;
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

private:
  CanonicalQueryKey keyField;
  DatabaseRevision changedAtField;
  Durability durabilityField;
  zc::Maybe<InputProbeObservation> inputProbeObservationField;
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
