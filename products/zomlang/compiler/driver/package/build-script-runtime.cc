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

#include "zomlang/compiler/driver/package/build-script-runtime.h"

#include "zc/core/encoding.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

constexpr uint64_t kMebibyte = 1024U * 1024U;

zc::Array<uint8_t> encodePath(const identity::CanonicalRelativePath& path) {
  identity::CanonicalEncoder encoder;
  path.encode(encoder);
  return encoder.finish();
}

void sortPaths(zc::Vector<identity::CanonicalRelativePath>& paths) {
  for (size_t index = 1; index < paths.size(); ++index) {
    auto current = zc::mv(paths[index]);
    const auto currentBytes = encodePath(current);
    size_t insertion = index;
    while (insertion != 0 && currentBytes.asPtr() < encodePath(paths[insertion - 1]).asPtr()) {
      paths[insertion] = zc::mv(paths[insertion - 1]);
      --insertion;
    }
    paths[insertion] = zc::mv(current);
  }
}

bool uniquePaths(zc::ArrayPtr<const identity::CanonicalRelativePath> paths) {
  for (size_t index = 1; index < paths.size(); ++index) {
    if (encodePath(paths[index - 1]).asPtr() == encodePath(paths[index]).asPtr()) { return false; }
  }
  return true;
}

void sortEnvironment(zc::Vector<BuildScriptEnvironmentValue>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && current.name() < values[insertion - 1].name()) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

bool uniqueEnvironment(zc::ArrayPtr<const BuildScriptEnvironmentValue> values) {
  for (size_t index = 1; index < values.size(); ++index) {
    if (values[index - 1].name() == values[index].name()) { return false; }
  }
  return true;
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

zc::Maybe<uint64_t> decodeUint64Prefix(zc::ArrayPtr<const uint8_t> input) {
  if (input.size() < 8) { return zc::none; }
  uint64_t value = 0;
  for (size_t index = 0; index < 8; ++index) { value = (value << 8U) | input[index]; }
  return value;
}

zc::Array<uint8_t> copyBytes(zc::ArrayPtr<const uint8_t> input) {
  zc::Vector<uint8_t> result(input.size());
  result.addAll(input);
  return result.releaseAsArray();
}

template <typename Value, typename Encode>
bool sortUniqueEncoded(zc::Vector<Value>& values, Encode encode) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = encode(current);
    size_t insertion = index;
    while (insertion != 0 && currentBytes.asPtr() < encode(values[insertion - 1]).asPtr()) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < values.size(); ++index) {
    if (encode(values[index - 1]).asPtr() == encode(values[index]).asPtr()) { return false; }
  }
  return true;
}

template <typename Value>
zc::Array<uint8_t> encodeIdentityValue(const Value& value) {
  identity::CanonicalEncoder encoder;
  value.encode(encoder);
  return encoder.finish();
}

bool uniqueDigestEntryKeys(zc::ArrayPtr<const identity::BuildScriptDigestEntry> values) {
  for (size_t index = 1; index < values.size(); ++index) {
    if (encodePath(values[index - 1].path()).asPtr() == encodePath(values[index].path()).asPtr()) {
      return false;
    }
  }
  return true;
}

bool uniqueEnvironmentEntryKeys(zc::ArrayPtr<const identity::BuildScriptEnvironmentEntry> values) {
  for (size_t index = 1; index < values.size(); ++index) {
    if (values[index - 1].name() == values[index].name()) { return false; }
  }
  return true;
}

BuildScriptIssue responseStatusIssue(BuildScriptResponseStatus status) {
  switch (status) {
    case BuildScriptResponseStatus::UndeclaredInput:
      return BuildScriptIssue::UndeclaredInput;
    case BuildScriptResponseStatus::UndeclaredEnvironment:
      return BuildScriptIssue::UndeclaredEnvironment;
    case BuildScriptResponseStatus::UndeclaredOutput:
      return BuildScriptIssue::UndeclaredOutput;
    case BuildScriptResponseStatus::UndeclaredExport:
      return BuildScriptIssue::UndeclaredExport;
    case BuildScriptResponseStatus::ScriptFailure:
      return BuildScriptIssue::ExecutionFailed;
    case BuildScriptResponseStatus::FileDescriptorLimit:
      return BuildScriptIssue::FileDescriptorLimit;
    case BuildScriptResponseStatus::EnvironmentValueLimit:
      return BuildScriptIssue::EnvironmentValueLimit;
    case BuildScriptResponseStatus::ExportedEnvironmentLimit:
      return BuildScriptIssue::ExportedEnvironmentLimit;
    case BuildScriptResponseStatus::ResponseFrameLimit:
      return BuildScriptIssue::ResponseFrameLimit;
    case BuildScriptResponseStatus::Success:
      break;
  }
  ZC_UNREACHABLE;
}

}  // namespace

BuildScriptLimitKey::BuildScriptLimitKey(BuildScriptLimitValues values) noexcept
    : limitValues(values) {}

BuildScriptLimitValues BuildScriptLimitKey::defaults() noexcept {
  return BuildScriptLimitValues{60'000,        120'000,           512 * kMebibyte, 16,
                                4'096,         256 * kMebibyte,   4 * kMebibyte,   4 * kMebibyte,
                                1 * kMebibyte, 4 * kMebibyte - 17};
}

zc::OneOf<BuildScriptLimitKey, BuildScriptLimitInvariantIssue> BuildScriptLimitKey::verify(
    BuildScriptLimitValues values) {
  if (values.cpuMilliseconds < 1'000 || values.cpuMilliseconds > 60'000) {
    return BuildScriptLimitInvariantIssue::CpuRange;
  }
  if (values.cpuMilliseconds % 1'000 != 0) {
    return BuildScriptLimitInvariantIssue::CpuGranularity;
  }
  if (values.wallMilliseconds < 1 || values.wallMilliseconds > 120'000) {
    return BuildScriptLimitInvariantIssue::WallRange;
  }
  if (values.memoryBytes < 16 * kMebibyte || values.memoryBytes > 512 * kMebibyte) {
    return BuildScriptLimitInvariantIssue::MemoryRange;
  }
  if (values.fileDescriptorCount < 8 || values.fileDescriptorCount > 16) {
    return BuildScriptLimitInvariantIssue::FileDescriptorRange;
  }
  if (values.fileCount < 1 || values.fileCount > 4'096) {
    return BuildScriptLimitInvariantIssue::FileCountRange;
  }
  if (values.outputBytes < 1 || values.outputBytes > 256 * kMebibyte) {
    return BuildScriptLimitInvariantIssue::OutputRange;
  }
  if (values.requestFrameBytes < 33 || values.requestFrameBytes > 4 * kMebibyte) {
    return BuildScriptLimitInvariantIssue::RequestFrameRange;
  }
  if (values.responseFrameBytes < 18 || values.responseFrameBytes > 4 * kMebibyte) {
    return BuildScriptLimitInvariantIssue::ResponseFrameRange;
  }
  if (values.environmentValueBytes < 1 || values.environmentValueBytes > kMebibyte) {
    return BuildScriptLimitInvariantIssue::EnvironmentValueRange;
  }
  if (values.exportedEnvironmentBytes < 1 || values.exportedEnvironmentBytes > 4 * kMebibyte) {
    return BuildScriptLimitInvariantIssue::ExportedEnvironmentRange;
  }
  if (values.environmentValueBytes > values.requestFrameBytes - 32 ||
      values.environmentValueBytes > values.responseFrameBytes - 17 ||
      values.exportedEnvironmentBytes > values.responseFrameBytes - 17) {
    return BuildScriptLimitInvariantIssue::FrameRelation;
  }
  return BuildScriptLimitKey(values);
}

const BuildScriptLimitValues& BuildScriptLimitKey::values() const noexcept { return limitValues; }

void BuildScriptLimitKey::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint64(limitValues.cpuMilliseconds);
  encoder.encodeUint64(limitValues.wallMilliseconds);
  encoder.encodeUint64(limitValues.memoryBytes);
  encoder.encodeUint32(limitValues.fileDescriptorCount);
  encoder.encodeUint64(limitValues.fileCount);
  encoder.encodeUint64(limitValues.outputBytes);
  encoder.encodeUint64(limitValues.requestFrameBytes);
  encoder.encodeUint64(limitValues.responseFrameBytes);
  encoder.encodeUint64(limitValues.environmentValueBytes);
  encoder.encodeUint64(limitValues.exportedEnvironmentBytes);
}

BuildScriptExecutableKey::BuildScriptExecutableKey(
    RegisteredTargetSelection&& target, const identity::Sha256Digest& imageDigest) noexcept
    : targetValue(zc::mv(target)), imageDigestValue(imageDigest) {}

BuildScriptExecutableKey BuildScriptExecutableKey::from(RegisteredTargetSelection&& target,
                                                        const identity::Sha256Digest& imageDigest) {
  return BuildScriptExecutableKey(zc::mv(target), imageDigest);
}

BuildScriptExecutableKey BuildScriptExecutableKey::clone() const {
  return BuildScriptExecutableKey(targetValue.clone(), imageDigestValue);
}

const RegisteredTargetSelection& BuildScriptExecutableKey::target() const noexcept {
  return targetValue;
}

const identity::Sha256Digest& BuildScriptExecutableKey::imageDigest() const noexcept {
  return imageDigestValue;
}

void BuildScriptExecutableKey::encode(identity::CanonicalEncoder& encoder) const {
  targetValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(BuildScriptExecutableFormat::StaticElf));
  encoder.encodeDigest(imageDigestValue);
}

TrustedBuildRuntimeKey::TrustedBuildRuntimeKey(
    zc::Array<uint8_t>&& runtimeAbi, zc::Vector<identity::Sha256Digest>&& objectDigests,
    const identity::Sha256Digest& symbolManifest, const identity::Sha256Digest& relocationManifest,
    const identity::Sha256Digest& operationManifest) noexcept
    : runtimeAbiValue(zc::mv(runtimeAbi)),
      objectDigestValues(zc::mv(objectDigests)),
      symbolManifestValue(symbolManifest),
      relocationManifestValue(relocationManifest),
      operationManifestValue(operationManifest) {}

TrustedBuildRuntimeKey TrustedBuildRuntimeKey::clone() const {
  zc::Vector<identity::Sha256Digest> digests(objectDigestValues.size());
  digests.addAll(objectDigestValues);
  return TrustedBuildRuntimeKey(copyBytes(runtimeAbiValue), zc::mv(digests), symbolManifestValue,
                                relocationManifestValue, operationManifestValue);
}

void TrustedBuildRuntimeKey::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeByteString(runtimeAbiValue);
  encoder.encodeSequenceSize(objectDigestValues.size());
  for (const auto& digest : objectDigestValues) { encoder.encodeDigest(digest); }
  encoder.encodeDigest(symbolManifestValue);
  encoder.encodeDigest(relocationManifestValue);
  encoder.encodeDigest(operationManifestValue);
}

BuildScriptExecutionKey::BuildScriptExecutionKey(
    identity::PreparatoryBuildScriptKey&& preparatory,
    identity::SemanticContextFingerprint&& preparatoryContext,
    BuildScriptExecutableKey&& executable, TrustedBuildRuntimeKey&& trustedRuntime,
    CanonicalBuildScriptManifest&& contract, identity::CrateKey&& rootCrate,
    zc::Vector<identity::CrateKey>&& reachableHostCrates,
    zc::Vector<identity::CrateDependencyEdgeKey>&& reachableHostEdges,
    zc::Vector<identity::BuildScriptDigestEntry>&& inputDigests,
    zc::Vector<identity::BuildScriptEnvironmentEntry>&& declaredEnvironment,
    BuildScriptLimitKey&& limits) noexcept
    : preparatoryValue(zc::mv(preparatory)),
      preparatoryContextValue(zc::mv(preparatoryContext)),
      executableValue(zc::mv(executable)),
      trustedRuntimeValue(zc::mv(trustedRuntime)),
      contractValue(zc::mv(contract)),
      rootCrateValue(zc::mv(rootCrate)),
      reachableHostCrateValues(zc::mv(reachableHostCrates)),
      reachableHostEdgeValues(zc::mv(reachableHostEdges)),
      inputDigestValues(zc::mv(inputDigests)),
      declaredEnvironmentValues(zc::mv(declaredEnvironment)),
      limitValue(zc::mv(limits)) {}

zc::Maybe<BuildScriptExecutionKey> BuildScriptExecutionKey::from(
    identity::PreparatoryBuildScriptKey&& preparatory,
    identity::SemanticContextFingerprint&& preparatoryContext,
    BuildScriptExecutableKey&& executable, TrustedBuildRuntimeKey&& trustedRuntime,
    CanonicalBuildScriptManifest&& contract, identity::CrateKey&& rootCrate,
    zc::Vector<identity::CrateKey>&& reachableHostCrates,
    zc::Vector<identity::CrateDependencyEdgeKey>&& reachableHostEdges,
    zc::Vector<identity::BuildScriptDigestEntry>&& inputDigests,
    zc::Vector<identity::BuildScriptEnvironmentEntry>&& declaredEnvironment,
    BuildScriptLimitKey&& limits) {
  if (!sortUniqueEncoded(reachableHostCrates, encodeIdentityValue<identity::CrateKey>) ||
      !sortUniqueEncoded(reachableHostEdges,
                         encodeIdentityValue<identity::CrateDependencyEdgeKey>) ||
      !sortUniqueEncoded(inputDigests, encodeIdentityValue<identity::BuildScriptDigestEntry>) ||
      !uniqueDigestEntryKeys(inputDigests) ||
      !sortUniqueEncoded(declaredEnvironment,
                         encodeIdentityValue<identity::BuildScriptEnvironmentEntry>) ||
      !uniqueEnvironmentEntryKeys(declaredEnvironment)) {
    return zc::none;
  }
  if (inputDigests.size() != contract.inputs().size() ||
      declaredEnvironment.size() != contract.environment().size()) {
    return zc::none;
  }
  for (size_t index = 0; index < inputDigests.size(); ++index) {
    if (encodePath(inputDigests[index].path()).asPtr() !=
        encodePath(contract.inputs()[index]).asPtr()) {
      return zc::none;
    }
  }
  for (size_t index = 0; index < declaredEnvironment.size(); ++index) {
    if (declaredEnvironment[index].name() != contract.environment()[index].text()) {
      return zc::none;
    }
  }
  return BuildScriptExecutionKey(
      zc::mv(preparatory), zc::mv(preparatoryContext), zc::mv(executable), zc::mv(trustedRuntime),
      zc::mv(contract), zc::mv(rootCrate), zc::mv(reachableHostCrates), zc::mv(reachableHostEdges),
      zc::mv(inputDigests), zc::mv(declaredEnvironment), zc::mv(limits));
}

const identity::PreparatoryBuildScriptKey& BuildScriptExecutionKey::preparatory() const noexcept {
  return preparatoryValue;
}

const CanonicalBuildScriptManifest& BuildScriptExecutionKey::contract() const noexcept {
  return contractValue;
}

zc::ArrayPtr<const identity::BuildScriptDigestEntry> BuildScriptExecutionKey::inputDigests()
    const noexcept {
  return inputDigestValues;
}

zc::ArrayPtr<const identity::BuildScriptEnvironmentEntry>
BuildScriptExecutionKey::declaredEnvironment() const noexcept {
  return declaredEnvironmentValues;
}

const BuildScriptLimitKey& BuildScriptExecutionKey::limits() const noexcept { return limitValue; }

void BuildScriptExecutionKey::encode(identity::CanonicalEncoder& encoder) const {
  preparatoryValue.encode(encoder);
  encoder.encodeDigest(preparatoryContextValue.digest());
  executableValue.encode(encoder);
  trustedRuntimeValue.encode(encoder);
  contractValue.encode(encoder);
  rootCrateValue.encode(encoder);
  encoder.encodeSequenceSize(reachableHostCrateValues.size());
  for (const auto& value : reachableHostCrateValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(reachableHostEdgeValues.size());
  for (const auto& value : reachableHostEdgeValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(inputDigestValues.size());
  for (const auto& value : inputDigestValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(declaredEnvironmentValues.size());
  for (const auto& value : declaredEnvironmentValues) { value.encode(encoder); }
  limitValue.encode(encoder);
}

zc::Array<uint8_t> BuildScriptExecutionKey::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

BuildScriptEnvironmentValue::BuildScriptEnvironmentValue(identity::SemanticEnvironmentName&& name,
                                                         zc::Array<uint8_t>&& value) noexcept
    : nameValue(zc::mv(name)), byteValues(zc::mv(value)) {}

BuildScriptEnvironmentValue BuildScriptEnvironmentValue::from(
    identity::SemanticEnvironmentName&& name, zc::Array<uint8_t>&& value) {
  return BuildScriptEnvironmentValue(zc::mv(name), zc::mv(value));
}

zc::StringPtr BuildScriptEnvironmentValue::name() const noexcept { return nameValue.text(); }
zc::ArrayPtr<const uint8_t> BuildScriptEnvironmentValue::value() const noexcept {
  return byteValues;
}

BuildScriptEnvironmentValue BuildScriptEnvironmentValue::clone() const {
  return BuildScriptEnvironmentValue(nameValue.clone(), copyBytes(byteValues));
}

BuildScriptRequestFrame::BuildScriptRequestFrame(zc::Array<uint8_t>&& frame,
                                                 uint64_t payloadBytes) noexcept
    : frameBytes(zc::mv(frame)), payloadByteCount(payloadBytes) {}

zc::OneOf<BuildScriptRequestFrame, BuildScriptIssue> BuildScriptRequestFrame::encode(
    zc::Vector<identity::CanonicalRelativePath>&& inputs,
    zc::Vector<BuildScriptEnvironmentValue>&& environment,
    zc::Vector<identity::CanonicalRelativePath>&& outputs, const BuildScriptLimitKey& limits) {
  sortPaths(inputs);
  sortPaths(outputs);
  sortEnvironment(environment);
  ZC_IREQUIRE(uniquePaths(inputs) && uniquePaths(outputs) && uniqueEnvironment(environment),
              "verified build-script contract must remain canonically unique");
  if (!uniquePaths(inputs) || !uniquePaths(outputs) || !uniqueEnvironment(environment)) {
    ZC_UNREACHABLE;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeUint64(0);
  encoder.encodeSequenceSize(inputs.size());
  for (const auto& path : inputs) { path.encode(encoder); }
  encoder.encodeSequenceSize(environment.size());
  for (const auto& value : environment) {
    if (value.value().size() > limits.values().environmentValueBytes) {
      return BuildScriptIssue::EnvironmentValueLimit;
    }
    auto name = identity::SemanticEnvironmentName::fromCanonical(value.name());
    ZC_IF_SOME(nameValue, name) { nameValue.encode(encoder); }
    else { ZC_UNREACHABLE; }
    encoder.encodeByteString(value.value());
  }
  encoder.encodeSequenceSize(outputs.size());
  for (const auto& path : outputs) { path.encode(encoder); }
  auto payload = encoder.finish();
  if (payload.size() > limits.values().requestFrameBytes) {
    return BuildScriptIssue::RequestFrameLimit;
  }
  zc::Vector<uint8_t> frame(payload.size() + 8);
  appendUint64(frame, payload.size());
  frame.addAll(payload);
  return BuildScriptRequestFrame(frame.releaseAsArray(), payload.size());
}

zc::ArrayPtr<const uint8_t> BuildScriptRequestFrame::bytes() const noexcept { return frameBytes; }
uint64_t BuildScriptRequestFrame::payloadBytes() const noexcept { return payloadByteCount; }

BuildScriptResponse::BuildScriptResponse(
    zc::Vector<BuildScriptEnvironmentValue>&& exportedEnvironment) noexcept
    : exportedValues(zc::mv(exportedEnvironment)) {}

BuildScriptResponse BuildScriptResponse::success(
    zc::Vector<BuildScriptEnvironmentValue>&& exportedEnvironment) {
  return BuildScriptResponse(zc::mv(exportedEnvironment));
}

zc::ArrayPtr<const BuildScriptEnvironmentValue> BuildScriptResponse::exportedEnvironment()
    const noexcept {
  return exportedValues;
}

BuildScriptResponseDecodeResult decodeBuildScriptResponse(zc::ArrayPtr<const uint8_t> frame,
                                                          const BuildScriptLimitKey& limits) {
  auto payloadLength = decodeUint64Prefix(frame);
  if (payloadLength == zc::none) { return BuildScriptIssue::MalformedResponse; }
  ZC_IF_SOME(length, payloadLength) {
    if (length > limits.values().responseFrameBytes) {
      return BuildScriptIssue::ResponseFrameLimit;
    }
    if (length != frame.size() - 8) { return BuildScriptIssue::MalformedResponse; }
    identity::CanonicalDecoder decoder(frame.slice(8, frame.size()));
    auto correlation = decoder.decodeUint64();
    auto statusByte = decoder.decodeUint8();
    if (correlation == zc::none || statusByte == zc::none) {
      return BuildScriptIssue::MalformedResponse;
    }
    ZC_IF_SOME(correlationValue, correlation) {
      if (correlationValue != 0) { return BuildScriptIssue::MalformedResponse; }
    }
    BuildScriptResponseStatus status = BuildScriptResponseStatus::Success;
    ZC_IF_SOME(value, statusByte) {
      if (value > static_cast<uint8_t>(BuildScriptResponseStatus::ResponseFrameLimit)) {
        return BuildScriptIssue::MalformedResponse;
      }
      status = static_cast<BuildScriptResponseStatus>(value);
    }
    if (status != BuildScriptResponseStatus::Success) {
      if (!decoder.finished() || length != 9) { return BuildScriptIssue::MalformedResponse; }
      return responseStatusIssue(status);
    }
    auto count = decoder.decodeSequenceSize(limits.values().responseFrameBytes / 16);
    if (count == zc::none) { return BuildScriptIssue::MalformedResponse; }
    zc::Vector<BuildScriptEnvironmentValue> exported;
    uint64_t cumulativeBytes = 0;
    ZC_IF_SOME(countValue, count) {
      for (uint64_t index = 0; index < countValue; ++index) {
        auto nameBytes = decoder.decodeByteString(255);
        auto valueBytes = decoder.decodeByteString(limits.values().environmentValueBytes);
        if (nameBytes == zc::none || valueBytes == zc::none) {
          return BuildScriptIssue::MalformedResponse;
        }
        ZC_IF_SOME(nameByteValues, nameBytes) {
          auto nameText = zc::heapString(nameByteValues.asChars());
          auto name = identity::SemanticEnvironmentName::fromCanonical(nameText);
          if (name == zc::none) { return BuildScriptIssue::MalformedResponse; }
          ZC_IF_SOME(valueByteValues, valueBytes) {
            if (valueByteValues.size() > limits.values().environmentValueBytes) {
              return BuildScriptIssue::EnvironmentValueLimit;
            }
            const uint64_t entryBytes = 16 + nameByteValues.size() + valueByteValues.size();
            if (cumulativeBytes > UINT64_MAX - entryBytes) {
              return BuildScriptIssue::ExportedEnvironmentLimit;
            }
            cumulativeBytes += entryBytes;
            if (cumulativeBytes > limits.values().exportedEnvironmentBytes) {
              return BuildScriptIssue::ExportedEnvironmentLimit;
            }
            ZC_IF_SOME(nameValue, name) {
              exported.add(
                  BuildScriptEnvironmentValue::from(zc::mv(nameValue), zc::mv(valueByteValues)));
            }
          }
        }
      }
    }
    if (!decoder.finished()) { return BuildScriptIssue::MalformedResponse; }
    for (size_t index = 1; index < exported.size(); ++index) {
      if (!(exported[index - 1].name() < exported[index].name())) {
        return BuildScriptIssue::MalformedResponse;
      }
    }
    return BuildScriptResponse::success(zc::mv(exported));
  }
  ZC_UNREACHABLE;
}

zc::OneOf<BuildScriptRequestFrame, BuildScriptIssue> BuildScriptContractVerifier::createRequest(
    const CanonicalBuildScriptManifest& contract,
    zc::Vector<BuildScriptEnvironmentValue>&& environment, const BuildScriptLimitKey& limits) {
  sortEnvironment(environment);
  if (!uniqueEnvironment(environment) || environment.size() != contract.environment().size()) {
    return BuildScriptIssue::UndeclaredEnvironment;
  }
  for (size_t index = 0; index < environment.size(); ++index) {
    if (environment[index].name() != contract.environment()[index].text()) {
      return BuildScriptIssue::UndeclaredEnvironment;
    }
  }
  zc::Vector<identity::CanonicalRelativePath> inputs(contract.inputs().size());
  for (const auto& input : contract.inputs()) { inputs.add(input.clone()); }
  zc::Vector<identity::CanonicalRelativePath> outputs(contract.outputs().size());
  for (const auto& output : contract.outputs()) { outputs.add(output.clone()); }
  return BuildScriptRequestFrame::encode(zc::mv(inputs), zc::mv(environment), zc::mv(outputs),
                                         limits);
}

zc::Maybe<BuildScriptIssue> BuildScriptContractVerifier::verifyExports(
    const CanonicalBuildScriptManifest& contract, const BuildScriptResponse& response) {
  for (const auto& exported : response.exportedEnvironment()) {
    bool declared = false;
    for (const auto& allowed : contract.exportedEnvironment()) {
      if (exported.name() == allowed.text()) {
        declared = true;
        break;
      }
    }
    if (!declared) { return BuildScriptIssue::UndeclaredExport; }
  }
  return zc::none;
}

zc::Maybe<BuildScriptIssue> BuildScriptContractVerifier::verifyOutputs(
    const CanonicalBuildScriptManifest& contract, const SourceTreeRecord& outputs,
    const BuildScriptLimitKey& limits) {
  for (const auto& declared : contract.outputs()) {
    bool found = false;
    for (const auto& actual : outputs.files()) {
      if (encodePath(declared).asPtr() == encodePath(actual.path()).asPtr()) {
        found = true;
        break;
      }
    }
    if (!found) { return BuildScriptIssue::MissingOutput; }
  }
  for (const auto& actual : outputs.files()) {
    bool declared = false;
    for (const auto& allowed : contract.outputs()) {
      if (encodePath(actual.path()).asPtr() == encodePath(allowed).asPtr()) {
        declared = true;
        break;
      }
    }
    if (!declared) { return BuildScriptIssue::UndeclaredOutput; }
  }
  if (outputs.files().size() > limits.values().fileCount) {
    return BuildScriptIssue::FileCountLimit;
  }
  uint64_t outputBytes = 0;
  for (const auto& file : outputs.files()) {
    if (outputBytes > UINT64_MAX - file.byteLength()) { return BuildScriptIssue::OutputSizeLimit; }
    outputBytes += file.byteLength();
    if (outputBytes > limits.values().outputBytes) { return BuildScriptIssue::OutputSizeLimit; }
  }
  return zc::none;
}

VerifiedBuildScriptRun::VerifiedBuildScriptRun(DigestVerifiedSourceSnapshot&& outputs,
                                               BuildScriptResponse&& response) noexcept
    : outputValue(zc::mv(outputs)), responseValue(zc::mv(response)) {}

VerifiedBuildScriptRun VerifiedBuildScriptRun::from(DigestVerifiedSourceSnapshot&& outputs,
                                                    BuildScriptResponse&& response) {
  return VerifiedBuildScriptRun(zc::mv(outputs), zc::mv(response));
}

const DigestVerifiedSourceSnapshot& VerifiedBuildScriptRun::outputSnapshot() const noexcept {
  return outputValue;
}

const SourceTreeRecord& VerifiedBuildScriptRun::outputs() const noexcept {
  return outputValue.record();
}

const BuildScriptResponse& VerifiedBuildScriptRun::response() const noexcept {
  return responseValue;
}

VerifiedBuildScriptResult::VerifiedBuildScriptResult(
    VerifiedBuildScriptRun&& run, identity::BuildScriptOutputRecord&& output) noexcept
    : runValue(zc::mv(run)), outputValue(zc::mv(output)) {}

VerifiedBuildScriptResult VerifiedBuildScriptResult::from(
    VerifiedBuildScriptRun&& run, identity::BuildScriptOutputRecord&& output) {
  return VerifiedBuildScriptResult(zc::mv(run), zc::mv(output));
}

const VerifiedBuildScriptRun& VerifiedBuildScriptResult::run() const noexcept { return runValue; }

const identity::BuildScriptOutputRecord& VerifiedBuildScriptResult::output() const noexcept {
  return outputValue;
}

namespace {

BuildScriptRunResult runSandbox(BuildScriptSandboxFactory& factory,
                                const BuildScriptRequestFrame& request,
                                const CanonicalBuildScriptManifest& contract,
                                const BuildScriptLimitKey& limits) {
  auto created = factory.create();
  if (created.is<BuildScriptIssue>()) { return created.get<BuildScriptIssue>(); }
  auto sandbox = zc::mv(created.get<zc::Own<BuildScriptSandboxAdapter>>());
  auto result = sandbox->execute(request);
  auto finishIssue = sandbox->finish();
  if (finishIssue != zc::none) {
    ZC_IF_SOME(issue, finishIssue) { return issue; }
  }
  if (result.is<BuildScriptIssue>()) { return result.get<BuildScriptIssue>(); }
  auto& run = result.get<VerifiedBuildScriptRun>();
  ZC_IF_SOME(issue, BuildScriptContractVerifier::verifyExports(contract, run.response())) {
    return issue;
  }
  ZC_IF_SOME(issue, BuildScriptContractVerifier::verifyOutputs(contract, run.outputs(), limits)) {
    return issue;
  }
  for (const auto& file : run.outputs().files()) {
    auto bytes = run.outputSnapshot().readVerifiedFile(file.path());
    if (!bytes.is<zc::Array<zc::byte>>() ||
        zc::encodeUtf32(bytes.get<zc::Array<zc::byte>>().asChars()) == zc::none) {
      return BuildScriptIssue::InvalidGeneratedSource;
    }
  }
  return zc::mv(run);
}

bool sameOutputBytes(const VerifiedBuildScriptRun& left, const VerifiedBuildScriptRun& right) {
  if (left.outputs().digest() != right.outputs().digest() ||
      left.outputs().files().size() != right.outputs().files().size()) {
    return false;
  }
  for (size_t index = 0; index < left.outputs().files().size(); ++index) {
    const auto& leftFile = left.outputs().files()[index];
    const auto& rightFile = right.outputs().files()[index];
    if (leftFile.encode().asPtr() != rightFile.encode().asPtr()) { return false; }
    auto leftBytes = left.outputSnapshot().readVerifiedFile(leftFile.path());
    auto rightBytes = right.outputSnapshot().readVerifiedFile(rightFile.path());
    if (!leftBytes.is<zc::Array<zc::byte>>() || !rightBytes.is<zc::Array<zc::byte>>()) {
      return false;
    }
    if (leftBytes.get<zc::Array<zc::byte>>().asPtr() !=
        rightBytes.get<zc::Array<zc::byte>>().asPtr()) {
      return false;
    }
  }
  return true;
}

zc::Maybe<identity::BuildScriptOutputRecord> buildOutputRecord(
    const BuildScriptExecutionKey& executionKey, const VerifiedBuildScriptRun& run) {
  zc::Vector<identity::BuildScriptDigestEntry> sources(executionKey.inputDigests().size());
  for (const auto& value : executionKey.inputDigests()) { sources.add(value.clone()); }
  zc::Vector<identity::BuildScriptEnvironmentEntry> environment(
      executionKey.declaredEnvironment().size());
  for (const auto& value : executionKey.declaredEnvironment()) { environment.add(value.clone()); }
  zc::Vector<identity::BuildScriptDigestEntry> generated(run.outputs().files().size());
  for (const auto& file : run.outputs().files()) {
    generated.add(
        identity::BuildScriptDigestEntry::from(file.path().clone(), file.contentDigest()));
  }
  zc::Vector<identity::BuildScriptEnvironmentEntry> exported(
      run.response().exportedEnvironment().size());
  for (const auto& value : run.response().exportedEnvironment()) {
    auto name = identity::SemanticEnvironmentName::fromCanonical(value.name());
    ZC_IF_SOME(nameValue, name) {
      exported.add(
          identity::BuildScriptEnvironmentEntry::from(zc::mv(nameValue), copyBytes(value.value())));
    }
    else { ZC_UNREACHABLE; }
  }
  return identity::BuildScriptOutputRecord::from(executionKey.preparatory().clone(),
                                                 zc::mv(sources), zc::mv(environment),
                                                 zc::mv(generated), zc::mv(exported));
}

}  // namespace

BuildScriptExecutionResult BuildScriptDeterminismExecutor::executeCacheMiss(
    BuildScriptSandboxFactory& factory, const BuildScriptExecutionKey& executionKey,
    const BuildScriptRequestFrame& request) {
  auto first = runSandbox(factory, request, executionKey.contract(), executionKey.limits());
  if (first.is<BuildScriptIssue>()) { return first.get<BuildScriptIssue>(); }
  auto second = runSandbox(factory, request, executionKey.contract(), executionKey.limits());
  if (second.is<BuildScriptIssue>()) { return second.get<BuildScriptIssue>(); }
  auto& firstRun = first.get<VerifiedBuildScriptRun>();
  const auto& secondRun = second.get<VerifiedBuildScriptRun>();
  auto firstOutput = buildOutputRecord(executionKey, firstRun);
  auto secondOutput = buildOutputRecord(executionKey, secondRun);
  if (firstOutput == zc::none || secondOutput == zc::none) {
    return BuildScriptIssue::InvalidGeneratedSource;
  }
  ZC_IF_SOME(firstOutputValue, firstOutput) {
    ZC_IF_SOME(secondOutputValue, secondOutput) {
      if (firstOutputValue.encode().asPtr() != secondOutputValue.encode().asPtr() ||
          !sameOutputBytes(firstRun, secondRun)) {
        return BuildScriptIssue::NondeterministicOutput;
      }
      return VerifiedBuildScriptResult::from(zc::mv(firstRun), zc::mv(firstOutputValue));
    }
  }
  else { return BuildScriptIssue::NondeterministicOutput; }
  ZC_UNREACHABLE;
}

struct BuildScriptCacheCandidate::Impl final {
  Impl(zc::Array<uint8_t>&& executionKeyBytes, zc::Array<uint8_t>&& outputRecordBytes,
       identity::BuildScriptOutputRecord&& output,
       DigestVerifiedSourceSnapshot&& generatedSnapshot) noexcept
      : executionKeyByteValues(zc::mv(executionKeyBytes)),
        outputRecordByteValues(zc::mv(outputRecordBytes)),
        outputValue(zc::mv(output)),
        generatedSnapshotValue(zc::mv(generatedSnapshot)) {}

  zc::Array<uint8_t> executionKeyByteValues;
  zc::Array<uint8_t> outputRecordByteValues;
  identity::BuildScriptOutputRecord outputValue;
  DigestVerifiedSourceSnapshot generatedSnapshotValue;
};

BuildScriptCacheCandidate::BuildScriptCacheCandidate(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

BuildScriptCacheCandidate BuildScriptCacheCandidate::from(
    zc::Array<uint8_t>&& executionKeyBytes, zc::Array<uint8_t>&& outputRecordBytes,
    identity::BuildScriptOutputRecord&& output, DigestVerifiedSourceSnapshot&& generatedSnapshot) {
  return BuildScriptCacheCandidate(zc::heap<Impl>(zc::mv(executionKeyBytes),
                                                  zc::mv(outputRecordBytes), zc::mv(output),
                                                  zc::mv(generatedSnapshot)));
}

BuildScriptCacheCandidate::~BuildScriptCacheCandidate() noexcept = default;
BuildScriptCacheCandidate::BuildScriptCacheCandidate(BuildScriptCacheCandidate&&) noexcept =
    default;
BuildScriptCacheCandidate& BuildScriptCacheCandidate::operator=(
    BuildScriptCacheCandidate&&) noexcept = default;

zc::ArrayPtr<const uint8_t> BuildScriptCacheCandidate::executionKeyBytes() const noexcept {
  return impl->executionKeyByteValues;
}

zc::ArrayPtr<const uint8_t> BuildScriptCacheCandidate::outputRecordBytes() const noexcept {
  return impl->outputRecordByteValues;
}

const identity::BuildScriptOutputRecord& BuildScriptCacheCandidate::output() const noexcept {
  return impl->outputValue;
}

const DigestVerifiedSourceSnapshot& BuildScriptCacheCandidate::generatedSnapshot() const noexcept {
  return impl->generatedSnapshotValue;
}

namespace {

template <typename Value>
bool sameEncodedSequence(zc::ArrayPtr<const Value> left, zc::ArrayPtr<const Value> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    identity::CanonicalEncoder leftEncoder;
    identity::CanonicalEncoder rightEncoder;
    left[index].encode(leftEncoder);
    right[index].encode(rightEncoder);
    if (leftEncoder.finish().asPtr() != rightEncoder.finish().asPtr()) { return false; }
  }
  return true;
}

zc::OneOf<BuildScriptResponse, BuildScriptIssue> revalidateCacheCandidate(
    const BuildScriptCacheCandidate& candidate, const BuildScriptExecutionKey& executionKey) {
  const auto expectedExecutionKey = executionKey.encode();
  const auto expectedOutputRecord = candidate.output().encode();
  if (candidate.executionKeyBytes() != expectedExecutionKey.asPtr() ||
      candidate.outputRecordBytes() != expectedOutputRecord.asPtr() ||
      candidate.output().preparatoryKey().encode().asPtr() !=
          executionKey.preparatory().encode().asPtr() ||
      !sameEncodedSequence(candidate.output().sourceDigests(), executionKey.inputDigests()) ||
      !sameEncodedSequence(candidate.output().declaredEnvironment(),
                           executionKey.declaredEnvironment())) {
    return BuildScriptIssue::BuildResultIntegrityViolation;
  }
  ZC_IF_SOME(issue, BuildScriptContractVerifier::verifyOutputs(
                        executionKey.contract(), candidate.generatedSnapshot().record(),
                        executionKey.limits())) {
    return issue;
  }
  const auto generated = candidate.output().generatedSources();
  const auto files = candidate.generatedSnapshot().record().files();
  if (generated.size() != files.size()) { return BuildScriptIssue::BuildResultIntegrityViolation; }
  for (size_t index = 0; index < generated.size(); ++index) {
    if (encodePath(generated[index].path()).asPtr() != encodePath(files[index].path()).asPtr() ||
        generated[index].digest() != files[index].contentDigest()) {
      return BuildScriptIssue::BuildResultIntegrityViolation;
    }
    auto bytes = candidate.generatedSnapshot().readVerifiedFile(files[index].path());
    if (!bytes.is<zc::Array<zc::byte>>() ||
        zc::encodeUtf32(bytes.get<zc::Array<zc::byte>>().asChars()) == zc::none) {
      return BuildScriptIssue::InvalidGeneratedSource;
    }
  }

  zc::Vector<BuildScriptEnvironmentValue> exported(candidate.output().exportedEnvironment().size());
  uint64_t cumulativeBytes = 0;
  for (const auto& value : candidate.output().exportedEnvironment()) {
    if (value.value().size() > executionKey.limits().values().environmentValueBytes ||
        value.value().size() > UINT64_MAX - value.name().size() - 16) {
      return BuildScriptIssue::BuildResultIntegrityViolation;
    }
    const uint64_t entryBytes = 16 + value.name().size() + value.value().size();
    if (cumulativeBytes > UINT64_MAX - entryBytes) {
      return BuildScriptIssue::BuildResultIntegrityViolation;
    }
    cumulativeBytes += entryBytes;
    if (cumulativeBytes > executionKey.limits().values().exportedEnvironmentBytes) {
      return BuildScriptIssue::BuildResultIntegrityViolation;
    }
    auto name = identity::SemanticEnvironmentName::fromCanonical(value.name());
    if (name == zc::none) { return BuildScriptIssue::BuildResultIntegrityViolation; }
    ZC_IF_SOME(nameValue, name) {
      exported.add(BuildScriptEnvironmentValue::from(zc::mv(nameValue), copyBytes(value.value())));
    }
  }
  auto response = BuildScriptResponse::success(zc::mv(exported));
  ZC_IF_SOME(issue, BuildScriptContractVerifier::verifyExports(executionKey.contract(), response)) {
    return issue;
  }
  return response;
}

}  // namespace

BuildScriptExecutionResult BuildScriptCacheExecutor::execute(
    BuildScriptCache& cache, BuildScriptSandboxFactory& factory,
    const BuildScriptExecutionKey& executionKey, const BuildScriptRequestFrame& request) {
  auto executionKeyBytes = executionKey.encode();
  auto lookup = cache.lookup(executionKeyBytes);
  if (lookup.is<BuildScriptIssue>()) { return lookup.get<BuildScriptIssue>(); }
  if (lookup.is<BuildScriptCacheCandidate>()) {
    auto candidate = zc::mv(lookup.get<BuildScriptCacheCandidate>());
    auto response = revalidateCacheCandidate(candidate, executionKey);
    if (response.is<BuildScriptIssue>()) { return response.get<BuildScriptIssue>(); }
    auto run = VerifiedBuildScriptRun::from(zc::mv(candidate.impl->generatedSnapshotValue),
                                            zc::mv(response.get<BuildScriptResponse>()));
    return VerifiedBuildScriptResult::from(zc::mv(run), zc::mv(candidate.impl->outputValue));
  }

  auto executed = BuildScriptDeterminismExecutor::executeCacheMiss(factory, executionKey, request);
  if (executed.is<BuildScriptIssue>()) { return executed.get<BuildScriptIssue>(); }
  auto& verified = executed.get<VerifiedBuildScriptResult>();
  ZC_IF_SOME(issue, cache.publishAtomically(executionKeyBytes, verified.output(),
                                            verified.run().outputSnapshot())) {
    return issue;
  }
  return zc::mv(verified);
}

struct VerifiedBuildScriptResultSet::Impl final {
  Impl(zc::Vector<identity::PreparatoryBuildScriptKey>&& planKeys,
       zc::Vector<VerifiedBuildScriptResult>&& results) noexcept
      : planKeyValues(zc::mv(planKeys)), resultValues(zc::mv(results)) {}

  zc::Vector<identity::PreparatoryBuildScriptKey> planKeyValues;
  zc::Vector<VerifiedBuildScriptResult> resultValues;
};

VerifiedBuildScriptResultSet::VerifiedBuildScriptResultSet(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

namespace {

void sortPlanKeys(zc::Vector<identity::PreparatoryBuildScriptKey>& keys) {
  for (size_t index = 1; index < keys.size(); ++index) {
    auto current = zc::mv(keys[index]);
    const auto currentBytes = current.encode();
    size_t insertion = index;
    while (insertion != 0 && currentBytes.asPtr() < keys[insertion - 1].encode().asPtr()) {
      keys[insertion] = zc::mv(keys[insertion - 1]);
      --insertion;
    }
    keys[insertion] = zc::mv(current);
  }
}

void sortBuildResults(zc::Vector<VerifiedBuildScriptResult>& results) {
  for (size_t index = 1; index < results.size(); ++index) {
    auto current = zc::mv(results[index]);
    const auto currentBytes = current.output().preparatoryKey().encode();
    size_t insertion = index;
    while (insertion != 0 &&
           currentBytes.asPtr() <
               results[insertion - 1].output().preparatoryKey().encode().asPtr()) {
      results[insertion] = zc::mv(results[insertion - 1]);
      --insertion;
    }
    results[insertion] = zc::mv(current);
  }
}

bool validFrozenResult(const VerifiedBuildScriptResult& result) {
  const auto generated = result.output().generatedSources();
  const auto files = result.run().outputs().files();
  if (generated.size() != files.size()) { return false; }
  for (size_t index = 0; index < generated.size(); ++index) {
    if (encodePath(generated[index].path()).asPtr() != encodePath(files[index].path()).asPtr() ||
        generated[index].digest() != files[index].contentDigest()) {
      return false;
    }
    auto bytes = result.run().outputSnapshot().readVerifiedFile(files[index].path());
    if (!bytes.is<zc::Array<zc::byte>>() ||
        zc::encodeUtf32(bytes.get<zc::Array<zc::byte>>().asChars()) == zc::none) {
      return false;
    }
  }
  return true;
}

}  // namespace

zc::Maybe<VerifiedBuildScriptResultSet> VerifiedBuildScriptResultSet::from(
    zc::Vector<identity::PreparatoryBuildScriptKey>&& planKeys,
    zc::Vector<VerifiedBuildScriptResult>&& results) {
  sortPlanKeys(planKeys);
  sortBuildResults(results);
  if (planKeys.size() != results.size()) { return zc::none; }
  for (size_t index = 0; index < planKeys.size(); ++index) {
    const auto keyBytes = planKeys[index].encode();
    if ((index != 0 && keyBytes.asPtr() == planKeys[index - 1].encode().asPtr()) ||
        keyBytes.asPtr() != results[index].output().preparatoryKey().encode().asPtr() ||
        !validFrozenResult(results[index])) {
      return zc::none;
    }
  }
  return VerifiedBuildScriptResultSet(zc::heap<Impl>(zc::mv(planKeys), zc::mv(results)));
}

VerifiedBuildScriptResultSet::~VerifiedBuildScriptResultSet() noexcept = default;
VerifiedBuildScriptResultSet::VerifiedBuildScriptResultSet(
    VerifiedBuildScriptResultSet&&) noexcept = default;
VerifiedBuildScriptResultSet& VerifiedBuildScriptResultSet::operator=(
    VerifiedBuildScriptResultSet&&) noexcept = default;

zc::ArrayPtr<const identity::PreparatoryBuildScriptKey> VerifiedBuildScriptResultSet::planKeys()
    const noexcept {
  return impl->planKeyValues;
}

zc::ArrayPtr<const VerifiedBuildScriptResult> VerifiedBuildScriptResultSet::results()
    const noexcept {
  return impl->resultValues;
}

}  // namespace zomlang::compiler::driver::package
