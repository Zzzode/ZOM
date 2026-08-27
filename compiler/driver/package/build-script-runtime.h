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
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "compiler/driver/package/manifest-model.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/driver/package/source-snapshot.h"
#include "compiler/driver/package/source-tree.h"
#include "compiler/identity/key/build-script-key.h"
#include "compiler/identity/canonical/canonical-scalar.h"
#include "compiler/identity/key/package-key.h"
#include "compiler/identity/semantic/context-fingerprint.h"

namespace zomlang::compiler::driver::package {

class TrustedRuntimeVerificationEvidence;

enum class BuildScriptIssue : uint8_t {
  SandboxUnavailable = 0x01,
  SandboxSetupFailed = 0x02,
  ForbiddenBuildCapability = 0x03,
  SeccompPolicyViolation = 0x04,
  OutputTreePolicyViolation = 0x05,
  ExecutableIdentityMismatch = 0x06,
  UndeclaredInput = 0x07,
  UndeclaredEnvironment = 0x08,
  UndeclaredExport = 0x09,
  MissingOutput = 0x0a,
  UndeclaredOutput = 0x0b,
  CpuLimit = 0x0c,
  WallLimit = 0x0d,
  MemoryLimit = 0x0e,
  FileDescriptorLimit = 0x0f,
  FileCountLimit = 0x10,
  OutputSizeLimit = 0x11,
  ExecutionFailed = 0x12,
  MalformedResponse = 0x13,
  RequestFrameLimit = 0x14,
  ResponseFrameLimit = 0x15,
  EnvironmentValueLimit = 0x16,
  ExportedEnvironmentLimit = 0x17,
  InvalidGeneratedSource = 0x18,
  NondeterministicOutput = 0x19,
  SandboxTeardownFailed = 0x1a,
  BuildResultIntegrityViolation = 0x1b,
};

enum class BuildScriptLimitInvariantIssue : uint8_t {
  CpuRange = 0x01,
  CpuGranularity = 0x02,
  WallRange = 0x03,
  MemoryRange = 0x04,
  FileDescriptorRange = 0x05,
  FileCountRange = 0x06,
  OutputRange = 0x07,
  RequestFrameRange = 0x08,
  ResponseFrameRange = 0x09,
  EnvironmentValueRange = 0x0a,
  ExportedEnvironmentRange = 0x0b,
  FrameRelation = 0x0c,
};

struct BuildScriptLimitValues final {
  uint64_t cpuMilliseconds;
  uint64_t wallMilliseconds;
  uint64_t memoryBytes;
  uint32_t fileDescriptorCount;
  uint64_t fileCount;
  uint64_t outputBytes;
  uint64_t requestFrameBytes;
  uint64_t responseFrameBytes;
  uint64_t environmentValueBytes;
  uint64_t exportedEnvironmentBytes;
};

/// \brief Structurally verified build-script resource and frame limits.
class BuildScriptLimitKey final {
public:
  ZC_NODISCARD static BuildScriptLimitValues defaults() noexcept;
  ZC_NODISCARD static zc::OneOf<BuildScriptLimitKey, BuildScriptLimitInvariantIssue> verify(
      BuildScriptLimitValues values);

  ZC_NODISCARD const BuildScriptLimitValues& values() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  explicit BuildScriptLimitKey(BuildScriptLimitValues values) noexcept;
  BuildScriptLimitValues limitValues;
};

enum class BuildScriptExecutableFormat : uint8_t { StaticElf = 0x01 };

/// \brief Verified static build-script image identity.
class BuildScriptExecutableKey final {
public:
  ZC_NODISCARD static BuildScriptExecutableKey from(RegisteredTargetSelection&& target,
                                                    const identity::Sha256Digest& imageDigest);
  BuildScriptExecutableKey(BuildScriptExecutableKey&&) noexcept = default;
  BuildScriptExecutableKey& operator=(BuildScriptExecutableKey&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptExecutableKey);

  ZC_NODISCARD BuildScriptExecutableKey clone() const;
  ZC_NODISCARD const RegisteredTargetSelection& target() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& imageDigest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  BuildScriptExecutableKey(RegisteredTargetSelection&& target,
                           const identity::Sha256Digest& imageDigest) noexcept;
  RegisteredTargetSelection targetValue;
  identity::Sha256Digest imageDigestValue;
};

enum class TrustedRuntimeInvariantIssue : uint8_t {
  EmptyObjectSet = 0x01,
  DuplicateObjectDigest = 0x02,
  RuntimeAbiMismatch = 0x03,
  ObjectDigestMismatch = 0x04,
  SymbolManifestMismatch = 0x05,
  RelocationManifestMismatch = 0x06,
  OperationManifestMismatch = 0x07,
  InvalidManifestRecord = 0x08,
  UnmanifestedSymbol = 0x09,
  UnmanifestedRelocation = 0x0a,
  WeakFallback = 0x0b,
  UnexpectedInitializer = 0x0c,
};

/// \brief Content-derived identity of the checked, statically linked build runtime.
class TrustedBuildRuntimeKey final {
public:
  ZC_NODISCARD static zc::OneOf<TrustedBuildRuntimeKey, TrustedRuntimeInvariantIssue>
  verifyEvidence(zc::StringPtr expectedRuntimeAbi, zc::StringPtr runtimeAbi,
                 zc::Vector<zc::Array<uint8_t>>&& objectBytes,
                 TrustedRuntimeVerificationEvidence&& evidence);
  TrustedBuildRuntimeKey(TrustedBuildRuntimeKey&&) noexcept = default;
  TrustedBuildRuntimeKey& operator=(TrustedBuildRuntimeKey&&) noexcept = default;
  ZC_DISALLOW_COPY(TrustedBuildRuntimeKey);

  ZC_NODISCARD TrustedBuildRuntimeKey clone() const;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  TrustedBuildRuntimeKey(zc::Array<uint8_t>&& runtimeAbi,
                         zc::Vector<identity::Sha256Digest>&& objectDigests,
                         const identity::Sha256Digest& symbolManifest,
                         const identity::Sha256Digest& relocationManifest,
                         const identity::Sha256Digest& operationManifest) noexcept;
  zc::Array<uint8_t> runtimeAbiValue;
  zc::Vector<identity::Sha256Digest> objectDigestValues;
  identity::Sha256Digest symbolManifestValue;
  identity::Sha256Digest relocationManifestValue;
  identity::Sha256Digest operationManifestValue;
};

/// \brief Complete cache and replay identity for one build-script execution.
class BuildScriptExecutionKey final {
public:
  ZC_NODISCARD static zc::Maybe<BuildScriptExecutionKey> from(
      identity::PreparatoryBuildScriptKey&& preparatory,
      identity::ContextFingerprint&& preparatoryContext,
      BuildScriptExecutableKey&& executable, TrustedBuildRuntimeKey&& trustedRuntime,
      CanonicalBuildScriptManifest&& contract, identity::CrateKey&& rootCrate,
      zc::Vector<identity::CrateKey>&& reachableHostCrates,
      zc::Vector<identity::CrateDependencyEdgeKey>&& reachableHostEdges,
      zc::Vector<identity::BuildScriptDigestEntry>&& inputDigests,
      zc::Vector<identity::BuildScriptEnvironmentEntry>&& declaredEnvironment,
      BuildScriptLimitKey&& limits);
  BuildScriptExecutionKey(BuildScriptExecutionKey&&) noexcept = default;
  BuildScriptExecutionKey& operator=(BuildScriptExecutionKey&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptExecutionKey);

  ZC_NODISCARD const identity::PreparatoryBuildScriptKey& preparatory() const noexcept;
  ZC_NODISCARD const CanonicalBuildScriptManifest& contract() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::BuildScriptDigestEntry> inputDigests() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::BuildScriptEnvironmentEntry> declaredEnvironment()
      const noexcept;
  ZC_NODISCARD const BuildScriptLimitKey& limits() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  BuildScriptExecutionKey(identity::PreparatoryBuildScriptKey&& preparatory,
                          identity::ContextFingerprint&& preparatoryContext,
                          BuildScriptExecutableKey&& executable,
                          TrustedBuildRuntimeKey&& trustedRuntime,
                          CanonicalBuildScriptManifest&& contract, identity::CrateKey&& rootCrate,
                          zc::Vector<identity::CrateKey>&& reachableHostCrates,
                          zc::Vector<identity::CrateDependencyEdgeKey>&& reachableHostEdges,
                          zc::Vector<identity::BuildScriptDigestEntry>&& inputDigests,
                          zc::Vector<identity::BuildScriptEnvironmentEntry>&& declaredEnvironment,
                          BuildScriptLimitKey&& limits) noexcept;
  identity::PreparatoryBuildScriptKey preparatoryValue;
  identity::ContextFingerprint preparatoryContextValue;
  BuildScriptExecutableKey executableValue;
  TrustedBuildRuntimeKey trustedRuntimeValue;
  CanonicalBuildScriptManifest contractValue;
  identity::CrateKey rootCrateValue;
  zc::Vector<identity::CrateKey> reachableHostCrateValues;
  zc::Vector<identity::CrateDependencyEdgeKey> reachableHostEdgeValues;
  zc::Vector<identity::BuildScriptDigestEntry> inputDigestValues;
  zc::Vector<identity::BuildScriptEnvironmentEntry> declaredEnvironmentValues;
  BuildScriptLimitKey limitValue;
};

/// \brief One canonical semantic environment name and its bounded arbitrary bytes.
class BuildScriptEnvironmentValue final {
public:
  ZC_NODISCARD static BuildScriptEnvironmentValue from(identity::SemanticEnvironmentName&& name,
                                                       zc::Array<uint8_t>&& value);

  BuildScriptEnvironmentValue(BuildScriptEnvironmentValue&&) noexcept = default;
  BuildScriptEnvironmentValue& operator=(BuildScriptEnvironmentValue&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptEnvironmentValue);

  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> value() const noexcept;
  ZC_NODISCARD BuildScriptEnvironmentValue clone() const;

private:
  BuildScriptEnvironmentValue(identity::SemanticEnvironmentName&& name,
                              zc::Array<uint8_t>&& value) noexcept;
  identity::SemanticEnvironmentName nameValue;
  zc::Array<uint8_t> byteValues;
};

/// \brief Complete length-prefixed canonical request frame for one sandbox execution.
class BuildScriptRequestFrame final {
public:
  ZC_NODISCARD static zc::OneOf<BuildScriptRequestFrame, BuildScriptIssue> encode(
      zc::Vector<identity::CanonicalRelativePath>&& inputs,
      zc::Vector<BuildScriptEnvironmentValue>&& environment,
      zc::Vector<identity::CanonicalRelativePath>&& outputs, const BuildScriptLimitKey& limits);

  BuildScriptRequestFrame(BuildScriptRequestFrame&&) noexcept = default;
  BuildScriptRequestFrame& operator=(BuildScriptRequestFrame&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptRequestFrame);

  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept;
  ZC_NODISCARD uint64_t payloadBytes() const noexcept;

private:
  BuildScriptRequestFrame(zc::Array<uint8_t>&& frame, uint64_t payloadBytes) noexcept;
  zc::Array<uint8_t> frameBytes;
  uint64_t payloadByteCount;
};

enum class BuildScriptResponseStatus : uint8_t {
  Success = 0x00,
  UndeclaredInput = 0x01,
  UndeclaredEnvironment = 0x02,
  UndeclaredOutput = 0x03,
  UndeclaredExport = 0x04,
  ScriptFailure = 0x05,
  FileDescriptorLimit = 0x06,
  EnvironmentValueLimit = 0x07,
  ExportedEnvironmentLimit = 0x08,
  ResponseFrameLimit = 0x09,
};

/// \brief Fully decoded canonical response admitted at the parent boundary.
class BuildScriptResponse final {
public:
  ZC_NODISCARD static BuildScriptResponse success(
      zc::Vector<BuildScriptEnvironmentValue>&& exportedEnvironment);

  BuildScriptResponse(BuildScriptResponse&&) noexcept = default;
  BuildScriptResponse& operator=(BuildScriptResponse&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptResponse);

  ZC_NODISCARD zc::ArrayPtr<const BuildScriptEnvironmentValue> exportedEnvironment() const noexcept;

private:
  explicit BuildScriptResponse(
      zc::Vector<BuildScriptEnvironmentValue>&& exportedEnvironment) noexcept;
  zc::Vector<BuildScriptEnvironmentValue> exportedValues;
};

using BuildScriptResponseDecodeResult = zc::OneOf<BuildScriptResponse, BuildScriptIssue>;

/// \brief Decodes one complete outer-length-prefixed build-script response frame.
ZC_NODISCARD BuildScriptResponseDecodeResult
decodeBuildScriptResponse(zc::ArrayPtr<const uint8_t> frame, const BuildScriptLimitKey& limits);

/// \brief Verifies request, response, and output facts against one declared build contract.
class BuildScriptContractVerifier final {
public:
  ZC_NODISCARD static zc::OneOf<BuildScriptRequestFrame, BuildScriptIssue> createRequest(
      const CanonicalBuildScriptManifest& contract,
      zc::Vector<BuildScriptEnvironmentValue>&& environment, const BuildScriptLimitKey& limits);
  ZC_NODISCARD static zc::Maybe<BuildScriptIssue> verifyExports(
      const CanonicalBuildScriptManifest& contract, const BuildScriptResponse& response);
  ZC_NODISCARD static zc::Maybe<BuildScriptIssue> verifyOutputs(
      const CanonicalBuildScriptManifest& contract, const SourceTreeRecord& outputs,
      const BuildScriptLimitKey& limits);
};

/// \brief One fully revalidated sandbox result before cache publication.
class VerifiedBuildScriptRun final {
public:
  ZC_NODISCARD static VerifiedBuildScriptRun from(DigestVerifiedSourceSnapshot&& outputs,
                                                  BuildScriptResponse&& response);

  VerifiedBuildScriptRun(VerifiedBuildScriptRun&&) noexcept = default;
  VerifiedBuildScriptRun& operator=(VerifiedBuildScriptRun&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedBuildScriptRun);

  ZC_NODISCARD const DigestVerifiedSourceSnapshot& outputSnapshot() const noexcept;
  ZC_NODISCARD const SourceTreeRecord& outputs() const noexcept;
  ZC_NODISCARD const BuildScriptResponse& response() const noexcept;

private:
  VerifiedBuildScriptRun(DigestVerifiedSourceSnapshot&& outputs,
                         BuildScriptResponse&& response) noexcept;
  DigestVerifiedSourceSnapshot outputValue;
  BuildScriptResponse responseValue;
};

using BuildScriptRunResult = zc::OneOf<VerifiedBuildScriptRun, BuildScriptIssue>;

/// \brief Boundary that detected an invalid build-result publication.
enum class BuildResultIntegrityProducer : uint8_t {
  DeterministicExecution = 0x01,
  CacheReplay = 0x02,
  FinalSessionPublication = 0x03,
};

/// \brief Exact relation that failed while publishing a build result.
enum class BuildResultIntegrityFact : uint8_t {
  ProducerKey = 0x01,
  SourceDigests = 0x02,
  DeclaredEnvironment = 0x03,
  GeneratedInventory = 0x04,
  GeneratedBytes = 0x05,
  ExportedEnvironment = 0x06,
  PlanAssociation = 0x07,
};

/// \brief Typed provenance for one build-result integrity failure.
class BuildResultIntegrityViolation final {
public:
  ZC_NODISCARD BuildResultIntegrityProducer producer() const noexcept;
  ZC_NODISCARD BuildResultIntegrityFact fact() const noexcept;

private:
  friend class VerifiedBuildScriptResult;
  friend class VerifiedBuildScriptResultSet;

  BuildResultIntegrityViolation(BuildResultIntegrityProducer producer,
                                BuildResultIntegrityFact fact) noexcept;
  BuildResultIntegrityProducer producerValue;
  BuildResultIntegrityFact factValue;
};

class VerifiedBuildScriptResult;
using BuildScriptResultPublication =
    zc::OneOf<VerifiedBuildScriptResult, BuildResultIntegrityViolation>;

/// \brief One deterministic run paired with its complete RFC 0011 output record.
class VerifiedBuildScriptResult final {
public:
  ZC_NODISCARD static BuildScriptResultPublication publishDeterministicExecution(
      const BuildScriptExecutionKey& executionKey, VerifiedBuildScriptRun&& run,
      identity::BuildScriptOutputRecord&& output);
  ZC_NODISCARD static BuildScriptResultPublication publishCacheReplay(
      const BuildScriptExecutionKey& executionKey, VerifiedBuildScriptRun&& run,
      identity::BuildScriptOutputRecord&& output);
  VerifiedBuildScriptResult(VerifiedBuildScriptResult&&) noexcept = default;
  VerifiedBuildScriptResult& operator=(VerifiedBuildScriptResult&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedBuildScriptResult);

  ZC_NODISCARD const VerifiedBuildScriptRun& run() const noexcept;
  ZC_NODISCARD const identity::BuildScriptOutputRecord& output() const noexcept;

private:
  ZC_NODISCARD static BuildScriptResultPublication publish(
      BuildResultIntegrityProducer producer, const BuildScriptExecutionKey& executionKey,
      VerifiedBuildScriptRun&& run, identity::BuildScriptOutputRecord&& output);
  VerifiedBuildScriptResult(VerifiedBuildScriptRun&& run,
                            identity::BuildScriptOutputRecord&& output) noexcept;
  VerifiedBuildScriptRun runValue;
  identity::BuildScriptOutputRecord outputValue;
};

using BuildScriptExecutionResult = zc::OneOf<VerifiedBuildScriptResult, BuildScriptIssue>;

class BuildScriptSandboxAdapter {
public:
  virtual ~BuildScriptSandboxAdapter() noexcept = default;
  ZC_NODISCARD virtual BuildScriptRunResult execute(const BuildScriptRequestFrame& request) = 0;
  ZC_NODISCARD virtual zc::Maybe<BuildScriptIssue> finish() = 0;
};

using BuildScriptSandboxCreateResult =
    zc::OneOf<zc::Own<BuildScriptSandboxAdapter>, BuildScriptIssue>;

class BuildScriptSandboxFactory {
public:
  virtual ~BuildScriptSandboxFactory() noexcept = default;
  ZC_NODISCARD virtual BuildScriptSandboxCreateResult create() = 0;
};

/// \brief Runs every cache miss in two independent sandboxes and compares complete results.
class BuildScriptDeterminismExecutor final {
public:
  ZC_NODISCARD static BuildScriptExecutionResult executeCacheMiss(
      BuildScriptSandboxFactory& factory, const BuildScriptExecutionKey& executionKey,
      const BuildScriptRequestFrame& request);
};

struct BuildScriptCacheMiss final {};

/// \brief Untrusted cache payload retained until all replay facts are reverified.
class BuildScriptCacheCandidate final {
public:
  ZC_NODISCARD static BuildScriptCacheCandidate from(
      zc::Array<uint8_t>&& executionKeyBytes, zc::Array<uint8_t>&& outputRecordBytes,
      identity::BuildScriptOutputRecord&& output, DigestVerifiedSourceSnapshot&& generatedSnapshot);
  ~BuildScriptCacheCandidate() noexcept;
  BuildScriptCacheCandidate(BuildScriptCacheCandidate&&) noexcept;
  BuildScriptCacheCandidate& operator=(BuildScriptCacheCandidate&&) noexcept;
  ZC_DISALLOW_COPY(BuildScriptCacheCandidate);

  ZC_NODISCARD zc::ArrayPtr<const uint8_t> executionKeyBytes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> outputRecordBytes() const noexcept;
  ZC_NODISCARD const identity::BuildScriptOutputRecord& output() const noexcept;
  ZC_NODISCARD const DigestVerifiedSourceSnapshot& generatedSnapshot() const noexcept;

private:
  struct Impl;
  explicit BuildScriptCacheCandidate(zc::Own<Impl>&& impl) noexcept;

  friend class BuildScriptCacheExecutor;
  zc::Own<Impl> impl;
};

using BuildScriptCacheLookupResult =
    zc::OneOf<BuildScriptCacheCandidate, BuildScriptCacheMiss, BuildScriptIssue>;

/// \brief Storage boundary whose publication operation must be atomic.
class BuildScriptCache {
public:
  virtual ~BuildScriptCache() noexcept(false) = default;
  ZC_NODISCARD virtual BuildScriptCacheLookupResult lookup(
      zc::ArrayPtr<const uint8_t> executionKeyBytes) = 0;
  ZC_NODISCARD virtual zc::Maybe<BuildScriptIssue> publishAtomically(
      zc::ArrayPtr<const uint8_t> executionKeyBytes,
      const identity::BuildScriptOutputRecord& output,
      const DigestVerifiedSourceSnapshot& generatedSnapshot) = 0;
};

/// \brief Revalidates cache hits or publishes one byte-equal double execution.
class BuildScriptCacheExecutor final {
public:
  ZC_NODISCARD static BuildScriptExecutionResult execute(
      BuildScriptCache& cache, BuildScriptSandboxFactory& factory,
      const BuildScriptExecutionKey& executionKey, const BuildScriptRequestFrame& request);
};

/// \brief Frozen exact-key result map admitted into one final package session.
class VerifiedBuildScriptResultSet final {
public:
  ZC_NODISCARD static zc::OneOf<VerifiedBuildScriptResultSet, BuildResultIntegrityViolation> from(
      zc::Vector<identity::PreparatoryBuildScriptKey>&& planKeys,
      zc::Vector<VerifiedBuildScriptResult>&& results);
  ~VerifiedBuildScriptResultSet() noexcept;
  VerifiedBuildScriptResultSet(VerifiedBuildScriptResultSet&&) noexcept;
  VerifiedBuildScriptResultSet& operator=(VerifiedBuildScriptResultSet&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBuildScriptResultSet);

  ZC_NODISCARD zc::ArrayPtr<const identity::PreparatoryBuildScriptKey> planKeys() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedBuildScriptResult> results() const noexcept;

private:
  struct Impl;
  explicit VerifiedBuildScriptResultSet(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::package
