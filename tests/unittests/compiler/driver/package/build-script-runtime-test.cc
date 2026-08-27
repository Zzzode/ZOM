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

#include "compiler/driver/package/build-script-runtime.h"

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "compiler/driver/package/manifest-parser.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

BuildScriptLimitKey limits(BuildScriptLimitValues values = BuildScriptLimitKey::defaults()) {
  auto result = BuildScriptLimitKey::verify(values);
  if (result.is<BuildScriptLimitKey>()) { return zc::mv(result.get<BuildScriptLimitKey>()); }
  ZC_FAIL_REQUIRE("valid build-script limits fixture was rejected");
}

BuildScriptLimitInvariantIssue invalid(BuildScriptLimitValues values) {
  auto result = BuildScriptLimitKey::verify(values);
  ZC_REQUIRE(result.is<BuildScriptLimitInvariantIssue>());
  return result.get<BuildScriptLimitInvariantIssue>();
}

identity::CanonicalRelativePath path(zc::StringPtr text) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical(text);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  ZC_REQUIRE(segments.size() == 1);
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::CanonicalRelativePath nestedPath(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  for (const auto text : {first, second}) {
    auto segment = identity::CanonicalPathSegment::fromCanonical(text);
    ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  }
  ZC_REQUIRE(segments.size() == 2);
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

CanonicalBuildScriptManifest contract() {
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(nestedPath("tools"_zc, "build.zom"_zc));
  files.add(nestedPath("data"_zc, "input.txt"_zc));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_IF_SOME(inventoryValue, inventory) {
    zc::Vector<identity::CanonicalPathSegment> documentSegments;
    auto documentSegment = identity::CanonicalPathSegment::fromCanonical("Zom.toml"_zc);
    ZC_IF_SOME(value, documentSegment) { documentSegments.add(zc::mv(value)); }
    ManifestParser parser;
    auto parsed = parser.parseWorkspaceManifest(
        identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
        R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build]
path = "tools/build.zom"
inputs = ["data/input.txt"]
outputs = ["generated/a.zom", "generated/b.zom"]
environment = ["HOME", "ZOM_TARGET"]
exported-environment = ["GENERATED_MODE"]
)toml"_zc,
        inventoryValue);
    if (parsed.is<NormalizedManifest>()) {
      return CanonicalBuildScriptManifest::from(parsed.get<NormalizedManifest>().buildScript());
    }
  }
  ZC_FAIL_REQUIRE("valid build-script contract fixture was rejected");
}

identity::Sha256Digest digest(zc::StringPtr text) {
  auto result = identity::sha256(text.asBytes());
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("digest fixture failed");
}

SourceTreeRecord outputTree(bool includeA, bool includeB, bool includeExtra = false,
                            uint64_t byteLength = 1) {
  zc::Vector<SourceTreeFile> files;
  if (includeA) {
    files.add(
        SourceTreeFile::from(nestedPath("generated"_zc, "a.zom"_zc), byteLength, digest("a"_zc)));
  }
  if (includeB) {
    files.add(
        SourceTreeFile::from(nestedPath("generated"_zc, "b.zom"_zc), byteLength, digest("b"_zc)));
  }
  if (includeExtra) {
    files.add(SourceTreeFile::from(nestedPath("generated"_zc, "extra.zom"_zc), byteLength,
                                   digest("x"_zc)));
  }
  auto result = SourceTreeRecord::from(zc::mv(files));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("output tree fixture failed");
}

identity::SemanticEnvironmentName environmentName(zc::StringPtr text);
zc::Array<uint8_t> bytes(zc::StringPtr text);

identity::SemanticEnvironmentName environmentName(zc::StringPtr text) {
  auto result = identity::SemanticEnvironmentName::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid environment name fixture");
}

zc::Array<uint8_t> bytes(zc::StringPtr text) {
  zc::Vector<uint8_t> values(text.size());
  for (const auto value : text) { values.add(static_cast<uint8_t>(value)); }
  return values.releaseAsArray();
}

zc::Array<uint8_t> frame(zc::Array<uint8_t>&& payload) {
  zc::Vector<uint8_t> result(payload.size() + 8);
  const uint64_t size = payload.size();
  for (int shift = 56; shift >= 0; shift -= 8) {
    result.add(static_cast<uint8_t>(size >> static_cast<uint32_t>(shift)));
  }
  result.addAll(payload);
  return result.releaseAsArray();
}

zc::Array<uint8_t> response(BuildScriptResponseStatus status,
                            zc::Vector<BuildScriptEnvironmentValue>&& exported = {}) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint64(0);
  encoder.encodeUint8(static_cast<uint8_t>(status));
  if (status == BuildScriptResponseStatus::Success) {
    encoder.encodeSequenceSize(exported.size());
    for (const auto& value : exported) {
      auto name = identity::SemanticEnvironmentName::fromCanonical(value.name());
      ZC_IF_SOME(nameValue, name) { nameValue.encode(encoder); }
      encoder.encodeByteString(value.value());
    }
  }
  return frame(encoder.finish());
}

}  // namespace

ZC_TEST("Build-script limit verifier classifies every invariant") {
  auto values = BuildScriptLimitKey::defaults();
  ZC_EXPECT(BuildScriptLimitKey::verify(values).is<BuildScriptLimitKey>());

  values.cpuMilliseconds = 999;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::CpuRange);
  values = BuildScriptLimitKey::defaults();
  values.cpuMilliseconds = 1'001;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::CpuGranularity);
  values = BuildScriptLimitKey::defaults();
  values.wallMilliseconds = 0;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::WallRange);
  values = BuildScriptLimitKey::defaults();
  values.memoryBytes = 1;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::MemoryRange);
  values = BuildScriptLimitKey::defaults();
  values.fileDescriptorCount = 7;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::FileDescriptorRange);
  values = BuildScriptLimitKey::defaults();
  values.fileCount = 0;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::FileCountRange);
  values = BuildScriptLimitKey::defaults();
  values.outputBytes = 0;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::OutputRange);
  values = BuildScriptLimitKey::defaults();
  values.requestFrameBytes = 32;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::RequestFrameRange);
  values = BuildScriptLimitKey::defaults();
  values.responseFrameBytes = 17;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::ResponseFrameRange);
  values = BuildScriptLimitKey::defaults();
  values.environmentValueBytes = 0;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::EnvironmentValueRange);
  values = BuildScriptLimitKey::defaults();
  values.exportedEnvironmentBytes = 0;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::ExportedEnvironmentRange);
  values = BuildScriptLimitKey::defaults();
  values.requestFrameBytes = 33;
  values.environmentValueBytes = 2;
  ZC_EXPECT(invalid(values) == BuildScriptLimitInvariantIssue::FrameRelation);
}

ZC_TEST("Build-script request framing has exact empty size and canonical ordering") {
  zc::Vector<identity::CanonicalRelativePath> noInputs;
  zc::Vector<BuildScriptEnvironmentValue> noEnvironment;
  zc::Vector<identity::CanonicalRelativePath> noOutputs;
  auto defaultLimits = limits();
  auto empty = BuildScriptRequestFrame::encode(zc::mv(noInputs), zc::mv(noEnvironment),
                                               zc::mv(noOutputs), defaultLimits);
  ZC_REQUIRE(empty.is<BuildScriptRequestFrame>());
  ZC_EXPECT(empty.get<BuildScriptRequestFrame>().payloadBytes() == 32);
  ZC_EXPECT(empty.get<BuildScriptRequestFrame>().bytes().size() == 40);

  zc::Vector<identity::CanonicalRelativePath> inputs;
  inputs.add(path("z"_zc));
  inputs.add(path("a"_zc));
  zc::Vector<BuildScriptEnvironmentValue> environment;
  environment.add(BuildScriptEnvironmentValue::from(environmentName("Z"_zc), bytes("2"_zc)));
  environment.add(BuildScriptEnvironmentValue::from(environmentName("A"_zc), bytes("1"_zc)));
  zc::Vector<identity::CanonicalRelativePath> outputs;
  outputs.add(path("out.zom"_zc));
  auto ordered = BuildScriptRequestFrame::encode(zc::mv(inputs), zc::mv(environment),
                                                 zc::mv(outputs), defaultLimits);
  ZC_REQUIRE(ordered.is<BuildScriptRequestFrame>());
  ZC_EXPECT(ordered.get<BuildScriptRequestFrame>().payloadBytes() > 32);
}

ZC_TEST("Build-script request framing enforces value and frame limits before execution") {
  auto values = BuildScriptLimitKey::defaults();
  values.requestFrameBytes = 33;
  values.responseFrameBytes = 18;
  values.environmentValueBytes = 1;
  values.exportedEnvironmentBytes = 1;
  auto small = limits(values);

  zc::Vector<identity::CanonicalRelativePath> noInputs;
  zc::Vector<BuildScriptEnvironmentValue> environment;
  environment.add(BuildScriptEnvironmentValue::from(environmentName("A"_zc), bytes("12"_zc)));
  zc::Vector<identity::CanonicalRelativePath> noOutputs;
  auto valueFailure = BuildScriptRequestFrame::encode(zc::mv(noInputs), zc::mv(environment),
                                                      zc::mv(noOutputs), small);
  ZC_REQUIRE(valueFailure.is<BuildScriptIssue>());
  ZC_EXPECT(valueFailure.get<BuildScriptIssue>() == BuildScriptIssue::EnvironmentValueLimit);

  zc::Vector<identity::CanonicalRelativePath> inputs;
  inputs.add(path("input"_zc));
  zc::Vector<BuildScriptEnvironmentValue> noEnvironment;
  zc::Vector<identity::CanonicalRelativePath> outputs;
  auto frameFailure = BuildScriptRequestFrame::encode(zc::mv(inputs), zc::mv(noEnvironment),
                                                      zc::mv(outputs), small);
  ZC_REQUIRE(frameFailure.is<BuildScriptIssue>());
  ZC_EXPECT(frameFailure.get<BuildScriptIssue>() == BuildScriptIssue::RequestFrameLimit);
}

ZC_TEST("Build-script response decoder accepts exact success and maps every failure status") {
  auto defaultLimits = limits();
  auto empty =
      decodeBuildScriptResponse(response(BuildScriptResponseStatus::Success), defaultLimits);
  ZC_REQUIRE(empty.is<BuildScriptResponse>());
  ZC_EXPECT(empty.get<BuildScriptResponse>().exportedEnvironment().size() == 0);

  const BuildScriptResponseStatus statuses[] = {
      BuildScriptResponseStatus::UndeclaredInput,
      BuildScriptResponseStatus::UndeclaredEnvironment,
      BuildScriptResponseStatus::UndeclaredOutput,
      BuildScriptResponseStatus::UndeclaredExport,
      BuildScriptResponseStatus::ScriptFailure,
      BuildScriptResponseStatus::FileDescriptorLimit,
      BuildScriptResponseStatus::EnvironmentValueLimit,
      BuildScriptResponseStatus::ExportedEnvironmentLimit,
      BuildScriptResponseStatus::ResponseFrameLimit,
  };
  const BuildScriptIssue issues[] = {
      BuildScriptIssue::UndeclaredInput,       BuildScriptIssue::UndeclaredEnvironment,
      BuildScriptIssue::UndeclaredOutput,      BuildScriptIssue::UndeclaredExport,
      BuildScriptIssue::ExecutionFailed,       BuildScriptIssue::FileDescriptorLimit,
      BuildScriptIssue::EnvironmentValueLimit, BuildScriptIssue::ExportedEnvironmentLimit,
      BuildScriptIssue::ResponseFrameLimit,
  };
  for (size_t index = 0; index < zc::size(statuses); ++index) {
    auto decoded = decodeBuildScriptResponse(response(statuses[index]), defaultLimits);
    ZC_REQUIRE(decoded.is<BuildScriptIssue>());
    ZC_EXPECT(decoded.get<BuildScriptIssue>() == issues[index]);
  }
}

ZC_TEST("Build-script response decoder rejects malformed and over-limit frames") {
  auto defaultLimits = limits();
  auto truncated = response(BuildScriptResponseStatus::Success);
  auto truncatedResult =
      decodeBuildScriptResponse(truncated.slice(0, truncated.size() - 1), defaultLimits);
  ZC_REQUIRE(truncatedResult.is<BuildScriptIssue>());
  ZC_EXPECT(truncatedResult.get<BuildScriptIssue>() == BuildScriptIssue::MalformedResponse);

  auto unknown = response(static_cast<BuildScriptResponseStatus>(0xff));
  auto unknownResult = decodeBuildScriptResponse(unknown, defaultLimits);
  ZC_REQUIRE(unknownResult.is<BuildScriptIssue>());
  ZC_EXPECT(unknownResult.get<BuildScriptIssue>() == BuildScriptIssue::MalformedResponse);

  auto values = BuildScriptLimitKey::defaults();
  values.responseFrameBytes = 18;
  values.environmentValueBytes = 1;
  values.exportedEnvironmentBytes = 1;
  auto small = limits(values);
  zc::Vector<BuildScriptEnvironmentValue> exported;
  exported.add(BuildScriptEnvironmentValue::from(environmentName("A"_zc), bytes("1"_zc)));
  auto oversized = decodeBuildScriptResponse(
      response(BuildScriptResponseStatus::Success, zc::mv(exported)), small);
  ZC_REQUIRE(oversized.is<BuildScriptIssue>());
  ZC_EXPECT(oversized.get<BuildScriptIssue>() == BuildScriptIssue::ResponseFrameLimit);
}

ZC_TEST("Build-script contract verifier binds declared environment and exports") {
  auto buildContract = contract();
  auto defaultLimits = limits();
  zc::Vector<BuildScriptEnvironmentValue> environment;
  environment.add(
      BuildScriptEnvironmentValue::from(environmentName("ZOM_TARGET"_zc), bytes("host"_zc)));
  environment.add(BuildScriptEnvironmentValue::from(environmentName("HOME"_zc), bytes("/home"_zc)));
  auto request =
      BuildScriptContractVerifier::createRequest(buildContract, zc::mv(environment), defaultLimits);
  ZC_REQUIRE(request.is<BuildScriptRequestFrame>());

  zc::Vector<BuildScriptEnvironmentValue> missing;
  missing.add(BuildScriptEnvironmentValue::from(environmentName("HOME"_zc), bytes("x"_zc)));
  auto rejected =
      BuildScriptContractVerifier::createRequest(buildContract, zc::mv(missing), defaultLimits);
  ZC_REQUIRE(rejected.is<BuildScriptIssue>());
  ZC_EXPECT(rejected.get<BuildScriptIssue>() == BuildScriptIssue::UndeclaredEnvironment);

  zc::Vector<BuildScriptEnvironmentValue> allowedExport;
  allowedExport.add(
      BuildScriptEnvironmentValue::from(environmentName("GENERATED_MODE"_zc), bytes("fast"_zc)));
  auto allowed = BuildScriptResponse::success(zc::mv(allowedExport));
  ZC_EXPECT(BuildScriptContractVerifier::verifyExports(buildContract, allowed) == zc::none);

  zc::Vector<BuildScriptEnvironmentValue> unknownExport;
  unknownExport.add(BuildScriptEnvironmentValue::from(environmentName("SECRET"_zc), bytes("x"_zc)));
  auto unknown = BuildScriptResponse::success(zc::mv(unknownExport));
  ZC_EXPECT(BuildScriptContractVerifier::verifyExports(buildContract, unknown) ==
            BuildScriptIssue::UndeclaredExport);
}

ZC_TEST("Build-script contract verifier enforces exact output set and resource limits") {
  auto buildContract = contract();
  auto defaultLimits = limits();
  auto exact = outputTree(true, true);
  ZC_EXPECT(BuildScriptContractVerifier::verifyOutputs(buildContract, exact, defaultLimits) ==
            zc::none);

  auto missing = outputTree(true, false);
  ZC_EXPECT(BuildScriptContractVerifier::verifyOutputs(buildContract, missing, defaultLimits) ==
            BuildScriptIssue::MissingOutput);
  auto extra = outputTree(true, true, true);
  ZC_EXPECT(BuildScriptContractVerifier::verifyOutputs(buildContract, extra, defaultLimits) ==
            BuildScriptIssue::UndeclaredOutput);

  auto values = BuildScriptLimitKey::defaults();
  values.fileCount = 1;
  auto oneFile = limits(values);
  ZC_EXPECT(BuildScriptContractVerifier::verifyOutputs(buildContract, exact, oneFile) ==
            BuildScriptIssue::FileCountLimit);
  values = BuildScriptLimitKey::defaults();
  values.outputBytes = 1;
  auto oneByte = limits(values);
  auto large = outputTree(true, true, false, 1);
  ZC_EXPECT(BuildScriptContractVerifier::verifyOutputs(buildContract, large, oneByte) ==
            BuildScriptIssue::OutputSizeLimit);
}

}  // namespace zomlang::compiler::driver::package
