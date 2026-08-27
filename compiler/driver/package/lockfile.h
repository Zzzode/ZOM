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
#include "zc/core/filesystem.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "compiler/driver/package/registry-record.h"
#include "compiler/identity/key/package-key.h"

namespace zomlang::compiler::driver::package {

enum class LockIssue : uint8_t {
  ReadFailed = 0x01,
  InvalidUtf8 = 0x02,
  TomlSyntax = 0x03,
  UnknownField = 0x04,
  MissingField = 0x05,
  WrongValueType = 0x06,
  NonCanonicalEncoding = 0x07,
  DuplicatePackageKey = 0x08,
  DuplicateEdge = 0x09,
  PackageKeyMismatch = 0x0a,
  SourceKeyMismatch = 0x0b,
  DanglingEdge = 0x0c,
  InvalidDigest = 0x0d,
  SourceFieldMismatch = 0x0e,
  TrustDomainMismatch = 0x0f,
  CurrentInputMismatch = 0x10
};

enum class LockWriteStage : uint8_t {
  TemporaryCreate = 0x01,
  Write = 0x02,
  FileSync = 0x03,
  Rename = 0x04,
  DirectorySync = 0x05
};

/// \brief One complete source-verified package entry in `Zom.lock`.
class LockPackageRecord final {
public:
  ZC_NODISCARD static zc::Maybe<LockPackageRecord> from(
      identity::PackageKey&& key, const identity::Sha256Digest& manifestDigest,
      const identity::Sha256Digest& sourceTreeDigest, zc::Maybe<ArchiveFormat> archiveFormat,
      zc::Maybe<identity::Sha256Digest> archiveDigest, zc::Maybe<SigningKeyId> signingKey);

  LockPackageRecord(LockPackageRecord&&) noexcept = default;
  LockPackageRecord& operator=(LockPackageRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(LockPackageRecord);

  ZC_NODISCARD LockPackageRecord clone() const;
  /// \brief Clone every allocating identity leaf through `resource`.
  /// \param resource Resource that must outlive the returned record.
  ZC_NODISCARD LockPackageRecord clone(zc::MemoryResource& resource) const;
  ZC_NODISCARD const identity::PackageKey& key() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& manifestDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceTreeDigest() const noexcept;
  ZC_NODISCARD bool hasArchive() const noexcept;
  ZC_NODISCARD ArchiveFormat archiveFormat() const;
  ZC_NODISCARD const identity::Sha256Digest& archiveDigest() const;
  ZC_NODISCARD const SigningKeyId& signingKey() const;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  LockPackageRecord(identity::PackageKey&& key, const identity::Sha256Digest& manifestDigest,
                    const identity::Sha256Digest& sourceTreeDigest,
                    zc::Maybe<ArchiveFormat> archiveFormat,
                    zc::Maybe<identity::Sha256Digest> archiveDigest,
                    zc::Maybe<SigningKeyId> signingKey) noexcept;

  identity::PackageKey keyValue;
  identity::Sha256Digest manifestDigestValue;
  identity::Sha256Digest sourceTreeDigestValue;
  zc::Maybe<ArchiveFormat> archiveFormatValue;
  zc::Maybe<identity::Sha256Digest> archiveDigestValue;
  zc::Maybe<SigningKeyId> signingKeyValue;
};

/// \brief Canonically sorted, internally closed lock package graph.
class VerifiedLockGraph final {
public:
  ZC_NODISCARD static zc::OneOf<VerifiedLockGraph, LockIssue> from(
      zc::Vector<LockPackageRecord>&& packages,
      zc::Vector<identity::PackageDependencyEdgeKey>&& edges);
  /// \brief Validate a graph while retaining all graph storage in `resource`.
  /// \param resource Resource that must outlive the returned graph.
  ZC_NODISCARD static zc::OneOf<VerifiedLockGraph, LockIssue> from(
      zc::MemoryResource& resource, zc::Vector<LockPackageRecord>&& packages,
      zc::Vector<identity::PackageDependencyEdgeKey>&& edges);

  VerifiedLockGraph(VerifiedLockGraph&&) noexcept = default;
  VerifiedLockGraph& operator=(VerifiedLockGraph&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedLockGraph);

  ZC_NODISCARD zc::ArrayPtr<const LockPackageRecord> packages() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::PackageDependencyEdgeKey> edges() const noexcept;
  ZC_NODISCARD VerifiedLockGraph clone() const;
  /// \brief Clone graph storage and all allocating identity leaves through `resource`.
  /// \param resource Resource that must outlive the returned graph.
  ZC_NODISCARD VerifiedLockGraph clone(zc::MemoryResource& resource) const;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  /// \brief Encode into an array owned by `resource`.
  /// \param resource Resource that must outlive the returned array.
  ZC_NODISCARD zc::Array<uint8_t> encode(zc::MemoryResource& resource) const;

private:
  VerifiedLockGraph(zc::Vector<LockPackageRecord>&& packages,
                    zc::Vector<identity::PackageDependencyEdgeKey>&& edges) noexcept;

  zc::Vector<LockPackageRecord> packageValues;
  zc::Vector<identity::PackageDependencyEdgeKey> edgeValues;
};

using LockParseResult = zc::OneOf<VerifiedLockGraph, LockIssue>;

struct LockReplayMetrics final {
  uint64_t solverInvocations = 0;
  uint64_t packageVisits = 0;
  uint64_t edgeVisits = 0;
};

/// \brief Validates and retains an exact current lock graph without invoking the resolver.
class LockedReplayVerifier final {
public:
  ZC_NODISCARD static zc::OneOf<VerifiedLockGraph, LockIssue> replay(
      const VerifiedLockGraph& locked, const VerifiedLockGraph& currentInput,
      zc::ArrayPtr<const identity::RegistryIdentity> trustedRegistries, LockReplayMetrics& metrics);
  /// \brief Validate replay and retain its graph through `resource` without solver work.
  /// \param resource Resource that must outlive the returned graph.
  ZC_NODISCARD static zc::OneOf<VerifiedLockGraph, LockIssue> replay(
      zc::MemoryResource& resource, const VerifiedLockGraph& locked,
      const VerifiedLockGraph& currentInput,
      zc::ArrayPtr<const identity::RegistryIdentity> trustedRegistries, LockReplayMetrics& metrics);
};

/// \brief Canonical TOML codec for the closed `Zom.lock` schema.
class LockfileCodec final {
public:
  ZC_NODISCARD static zc::String write(const VerifiedLockGraph& graph);
  ZC_NODISCARD static LockParseResult parse(zc::ArrayPtr<const zc::byte> source);
  ZC_NODISCARD static LockParseResult read(const zc::ReadableDirectory& workspaceRoot);
};

/// \brief Performs durable same-directory `Zom.lock` replacement.
class AtomicLockfileWriter final {
public:
  ZC_NODISCARD static zc::Maybe<LockWriteStage> write(
      const zc::Directory& workspaceRoot, zc::StringPtr canonicalLockfile,
      zc::Maybe<LockWriteStage> injectedFailure = zc::none);
};

}  // namespace zomlang::compiler::driver::package
