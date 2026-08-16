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

#include "zomlang/compiler/driver/package/manifest-model.h"

#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

bool validInputDocumentKind(InputDocumentKind kind) {
  return kind == InputDocumentKind::Manifest || kind == InputDocumentKind::Lock;
}

bool validTargetKind(identity::CrateTargetKind kind) {
  return kind == identity::CrateTargetKind::Library || kind == identity::CrateTargetKind::Binary ||
         kind == identity::CrateTargetKind::Test || kind == identity::CrateTargetKind::Benchmark ||
         kind == identity::CrateTargetKind::Example ||
         kind == identity::CrateTargetKind::BuildScript;
}

zc::Array<uint8_t> encodePath(const identity::CanonicalWorkspaceRelativePath& path) {
  identity::CanonicalEncoder encoder;
  path.encode(encoder);
  return encoder.finish();
}

template <typename Value>
zc::Array<uint8_t> encodeValue(const Value& value) {
  identity::CanonicalEncoder encoder;
  value.encode(encoder);
  return encoder.finish();
}

template <typename Value>
bool sortEncodedUnique(zc::Vector<Value>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           encodeValue(current).asPtr() < encodeValue(values[insertion - 1]).asPtr()) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < values.size(); ++index) {
    if (encodeValue(values[index - 1]).asPtr() == encodeValue(values[index]).asPtr()) {
      return false;
    }
  }
  return true;
}

bool encodedLess(const identity::CanonicalWorkspaceRelativePath& left,
                 const identity::CanonicalWorkspaceRelativePath& right) {
  return encodePath(left).asPtr() < encodePath(right).asPtr();
}

void sortMembers(zc::Vector<identity::CanonicalWorkspaceRelativePath>& members) {
  for (size_t index = 1; index < members.size(); ++index) {
    auto current = zc::mv(members[index]);
    size_t insertion = index;
    while (insertion > 0 && encodedLess(current, members[insertion - 1])) {
      members[insertion] = zc::mv(members[insertion - 1]);
      --insertion;
    }
    members[insertion] = zc::mv(current);
  }
}

bool encodedEqual(const identity::CanonicalWorkspaceRelativePath& left,
                  const identity::CanonicalWorkspaceRelativePath& right) {
  return encodePath(left).asPtr() == encodePath(right).asPtr();
}

template <typename Value>
zc::Vector<Value> cloneValues(zc::MemoryResource& resource, zc::ArrayPtr<const Value> source) {
  zc::Vector<Value> result(resource, source.size());
  for (const auto& value : source) { result.add(value.clone(resource)); }
  return result;
}

}  // namespace

DiagnosticDocumentPath::DiagnosticDocumentPath(WorkspaceDiagnosticDocumentPath&& path) noexcept
    : value(zc::mv(path)) {}

DiagnosticDocumentPath::DiagnosticDocumentPath(PackageDiagnosticDocumentPath&& path) noexcept
    : value(zc::mv(path)) {}

DiagnosticDocumentPath DiagnosticDocumentPath::workspace(
    identity::CanonicalWorkspaceRelativePath&& path) {
  return DiagnosticDocumentPath(WorkspaceDiagnosticDocumentPath{zc::mv(path)});
}

DiagnosticDocumentPath DiagnosticDocumentPath::package(
    const identity::Sha256Digest& sourceDigest, identity::CanonicalRelativePath&& relativePath) {
  return DiagnosticDocumentPath(PackageDiagnosticDocumentPath{sourceDigest, zc::mv(relativePath)});
}

DiagnosticDocumentPath DiagnosticDocumentPath::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(path, WorkspaceDiagnosticDocumentPath) {
      return workspace(path.path.clone());
    }  // namespace zomlang::compiler::driver::package
    ZC_CASE_ONEOF(path, PackageDiagnosticDocumentPath) {
      return package(path.sourceDigest, path.relativePath.clone());
    }
  }
  ZC_UNREACHABLE
}

DiagnosticDocumentPathKind DiagnosticDocumentPath::kind() const noexcept {
  return value.is<WorkspaceDiagnosticDocumentPath>() ? DiagnosticDocumentPathKind::Workspace
                                                     : DiagnosticDocumentPathKind::Package;
}

const identity::CanonicalWorkspaceRelativePath& DiagnosticDocumentPath::workspacePath() const {
  ZC_IREQUIRE(kind() == DiagnosticDocumentPathKind::Workspace,
              "Workspace diagnostic path required.");
  return value.get<WorkspaceDiagnosticDocumentPath>().path;
}

const identity::Sha256Digest& DiagnosticDocumentPath::packageSourceDigest() const {
  ZC_IREQUIRE(kind() == DiagnosticDocumentPathKind::Package, "Package diagnostic path required.");
  return value.get<PackageDiagnosticDocumentPath>().sourceDigest;
}

const identity::CanonicalRelativePath& DiagnosticDocumentPath::packageRelativePath() const {
  ZC_IREQUIRE(kind() == DiagnosticDocumentPathKind::Package, "Package diagnostic path required.");
  return value.get<PackageDiagnosticDocumentPath>().relativePath;
}

void DiagnosticDocumentPath::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(path, WorkspaceDiagnosticDocumentPath) { path.path.encode(encoder); }
    ZC_CASE_ONEOF(path, PackageDiagnosticDocumentPath) {
      encoder.encodeDigest(path.sourceDigest);
      path.relativePath.encode(encoder);
    }
  }
}

zc::Array<uint8_t> DiagnosticDocumentPath::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

InputDocumentKey::InputDocumentKey(InputDocumentKind kind, DiagnosticDocumentPath&& path,
                                   const identity::Sha256Digest& contentDigest) noexcept
    : kindValue(kind), pathValue(zc::mv(path)), digestValue(contentDigest) {}

zc::Maybe<InputDocumentKey> InputDocumentKey::from(InputDocumentKind kind,
                                                   DiagnosticDocumentPath&& path,
                                                   const identity::Sha256Digest& contentDigest) {
  if (!validInputDocumentKind(kind)) { return zc::none; }
  return InputDocumentKey(kind, zc::mv(path), contentDigest);
}

InputDocumentKey InputDocumentKey::clone() const {
  return InputDocumentKey(kindValue, pathValue.clone(), digestValue);
}

InputDocumentKind InputDocumentKey::kind() const noexcept { return kindValue; }
const DiagnosticDocumentPath& InputDocumentKey::path() const noexcept { return pathValue; }

const identity::Sha256Digest& InputDocumentKey::contentDigest() const noexcept {
  return digestValue;
}

void InputDocumentKey::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  pathValue.encode(encoder);
  encoder.encodeDigest(digestValue);
}

zc::Array<uint8_t> InputDocumentKey::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

ManifestSpan::ManifestSpan(InputDocumentKey&& document, uint64_t byteStart,
                           uint64_t byteEnd) noexcept
    : documentValue(zc::mv(document)), startValue(byteStart), endValue(byteEnd) {}

zc::Maybe<ManifestSpan> ManifestSpan::from(InputDocumentKey&& document, uint64_t documentByteLength,
                                           uint64_t byteStart, uint64_t byteEnd) {
  if (byteStart > byteEnd || byteEnd > documentByteLength) { return zc::none; }
  return ManifestSpan(zc::mv(document), byteStart, byteEnd);
}

ManifestSpan ManifestSpan::clone() const {
  return ManifestSpan(documentValue.clone(), startValue, endValue);
}

const InputDocumentKey& ManifestSpan::document() const noexcept { return documentValue; }

uint64_t ManifestSpan::byteStart() const noexcept { return startValue; }
uint64_t ManifestSpan::byteEnd() const noexcept { return endValue; }

void ManifestSpan::encode(identity::CanonicalEncoder& encoder) const {
  documentValue.encode(encoder);
  encoder.encodeUint64(startValue);
  encoder.encodeUint64(endValue);
}

DiagnosticAnchor::DiagnosticAnchor(ManifestSpan&& span) noexcept : value(zc::mv(span)) {}

DiagnosticAnchor DiagnosticAnchor::manifest(ManifestSpan&& span) {
  return DiagnosticAnchor(zc::mv(span));
}

DiagnosticAnchor DiagnosticAnchor::clone() const {
  return manifest(value.get<ManifestSpan>().clone());
}

DiagnosticAnchorKind DiagnosticAnchor::kind() const noexcept {
  return DiagnosticAnchorKind::Manifest;
}

const ManifestSpan& DiagnosticAnchor::manifestSpan() const {
  ZC_IREQUIRE(kind() == DiagnosticAnchorKind::Manifest,
              "manifestSpan requires a manifest diagnostic anchor");
  return value.get<ManifestSpan>();
}

void DiagnosticAnchor::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  value.get<ManifestSpan>().encode(encoder);
}

zc::Array<uint8_t> DiagnosticAnchor::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

PackageManifest::PackageManifest(identity::PackageName&& name, identity::ResolvedVersion&& version,
                                 uint32_t editionYear) noexcept
    : nameValue(zc::mv(name)), versionValue(zc::mv(version)), editionYearValue(editionYear) {}

PackageManifest PackageManifest::from(identity::PackageName&& name,
                                      identity::ResolvedVersion&& version, uint32_t editionYear) {
  return PackageManifest(zc::mv(name), zc::mv(version), editionYear);
}

PackageManifest PackageManifest::clone() const {
  return PackageManifest(nameValue.clone(), versionValue.clone(), editionYearValue);
}

PackageManifest PackageManifest::clone(zc::MemoryResource& resource) const {
  return PackageManifest(nameValue.clone(resource), versionValue.clone(resource), editionYearValue);
}

zc::StringPtr PackageManifest::name() const noexcept { return nameValue.text(); }
zc::StringPtr PackageManifest::version() const noexcept { return versionValue.text(); }
uint32_t PackageManifest::editionYear() const noexcept { return editionYearValue; }

void PackageManifest::encode(identity::CanonicalEncoder& encoder) const {
  nameValue.encode(encoder);
  versionValue.encode(encoder);
  encoder.encodeUint32(editionYearValue);
}

zc::Array<uint8_t> PackageManifest::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

WorkspaceManifest::WorkspaceManifest(
    zc::Vector<identity::CanonicalWorkspaceRelativePath>&& members) noexcept
    : memberValues(zc::mv(members)) {}

zc::Maybe<WorkspaceManifest> WorkspaceManifest::from(
    zc::Vector<identity::CanonicalWorkspaceRelativePath>&& members) {
  sortMembers(members);
  for (size_t index = 1; index < members.size(); ++index) {
    if (encodedEqual(members[index - 1], members[index])) { return zc::none; }
  }
  return WorkspaceManifest(zc::mv(members));
}

WorkspaceManifest WorkspaceManifest::clone() const {
  zc::Vector<identity::CanonicalWorkspaceRelativePath> result(memberValues.size());
  for (const auto& member : memberValues) { result.add(member.clone()); }
  return WorkspaceManifest(zc::mv(result));
}

WorkspaceManifest WorkspaceManifest::clone(zc::MemoryResource& resource) const {
  return WorkspaceManifest(cloneValues(resource, memberValues.asPtr()));
}

zc::ArrayPtr<const identity::CanonicalWorkspaceRelativePath> WorkspaceManifest::members()
    const noexcept {
  return memberValues.asPtr();
}

void WorkspaceManifest::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(memberValues.size());
  for (const auto& member : memberValues) { member.encode(encoder); }
}

zc::Array<uint8_t> WorkspaceManifest::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

TargetManifest::TargetManifest(identity::CrateTargetKind kind, identity::TargetName&& name,
                               identity::CanonicalRelativePath&& path, bool implicit,
                               DiagnosticAnchor&& origin) noexcept
    : kindValue(kind),
      nameValue(zc::mv(name)),
      pathValue(zc::mv(path)),
      implicitValue(implicit),
      originValue(zc::mv(origin)) {}

zc::Maybe<TargetManifest> TargetManifest::from(identity::CrateTargetKind kind,
                                               identity::TargetName&& name,
                                               identity::CanonicalRelativePath&& path,
                                               bool implicit, DiagnosticAnchor&& origin) {
  if (!validTargetKind(kind)) { return zc::none; }
  return TargetManifest(kind, zc::mv(name), zc::mv(path), implicit, zc::mv(origin));
}

TargetManifest TargetManifest::clone() const {
  return TargetManifest(kindValue, nameValue.clone(), pathValue.clone(), implicitValue,
                        originValue.clone());
}

identity::CrateTargetKind TargetManifest::kind() const noexcept { return kindValue; }
zc::StringPtr TargetManifest::name() const noexcept { return nameValue.text(); }
const identity::CanonicalRelativePath& TargetManifest::path() const noexcept { return pathValue; }
bool TargetManifest::implicit() const noexcept { return implicitValue; }
const DiagnosticAnchor& TargetManifest::origin() const noexcept { return originValue; }

void TargetManifest::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  nameValue.encode(encoder);
  pathValue.encode(encoder);
  encoder.encodeBool(implicitValue);
  originValue.encode(encoder);
}

CanonicalTargetManifest::CanonicalTargetManifest(identity::CrateTargetKind kind,
                                                 identity::TargetName&& name,
                                                 identity::CanonicalRelativePath&& path,
                                                 bool implicit) noexcept
    : kindValue(kind), nameValue(zc::mv(name)), pathValue(zc::mv(path)), implicitValue(implicit) {}

zc::Maybe<CanonicalTargetManifest> CanonicalTargetManifest::from(
    identity::CrateTargetKind kind, identity::TargetName&& name,
    identity::CanonicalRelativePath&& path, bool implicit) {
  if (!validTargetKind(kind)) { return zc::none; }
  return CanonicalTargetManifest(kind, zc::mv(name), zc::mv(path), implicit);
}

CanonicalTargetManifest CanonicalTargetManifest::from(const TargetManifest& target) {
  auto name = identity::TargetName::fromCanonical(target.name());
  ZC_IF_SOME(admitted, name) {
    return CanonicalTargetManifest(target.kind(), zc::mv(admitted), target.path().clone(),
                                   target.implicit());
  }
  ZC_IREQUIRE(false, "normalized target name must remain canonical");
  ZC_UNREACHABLE
}

CanonicalTargetManifest CanonicalTargetManifest::clone() const {
  return CanonicalTargetManifest(kindValue, nameValue.clone(), pathValue.clone(), implicitValue);
}

CanonicalTargetManifest CanonicalTargetManifest::clone(zc::MemoryResource& resource) const {
  return CanonicalTargetManifest(kindValue, nameValue.clone(resource), pathValue.clone(resource),
                                 implicitValue);
}

identity::CrateTargetKind CanonicalTargetManifest::kind() const noexcept { return kindValue; }
zc::StringPtr CanonicalTargetManifest::name() const noexcept { return nameValue.text(); }
const identity::CanonicalRelativePath& CanonicalTargetManifest::path() const noexcept {
  return pathValue;
}
bool CanonicalTargetManifest::implicit() const noexcept { return implicitValue; }

void CanonicalTargetManifest::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  nameValue.encode(encoder);
  pathValue.encode(encoder);
  encoder.encodeBool(implicitValue);
}

zc::Array<uint8_t> CanonicalTargetManifest::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

bool sortTargetManifests(zc::Vector<TargetManifest>& targets) {
  for (size_t index = 1; index < targets.size(); ++index) {
    auto current = zc::mv(targets[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           CanonicalTargetManifest::from(current).encode().asPtr() <
               CanonicalTargetManifest::from(targets[insertion - 1]).encode().asPtr()) {
      targets[insertion] = zc::mv(targets[insertion - 1]);
      --insertion;
    }
    targets[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < targets.size(); ++index) {
    if (CanonicalTargetManifest::from(targets[index - 1]).encode().asPtr() ==
        CanonicalTargetManifest::from(targets[index]).encode().asPtr()) {
      return false;
    }
  }
  return true;
}

BuildScriptManifest::BuildScriptManifest(
    TargetManifest&& target, zc::Vector<identity::CanonicalRelativePath>&& inputs,
    zc::Vector<identity::CanonicalRelativePath>&& outputs,
    zc::Vector<identity::SemanticEnvironmentName>&& environment,
    zc::Vector<identity::SemanticEnvironmentName>&& exportedEnvironment) noexcept
    : targetValue(zc::mv(target)),
      inputValues(zc::mv(inputs)),
      outputValues(zc::mv(outputs)),
      environmentValues(zc::mv(environment)),
      exportedEnvironmentValues(zc::mv(exportedEnvironment)) {}

zc::Maybe<BuildScriptManifest> BuildScriptManifest::from(
    TargetManifest&& target, zc::Vector<identity::CanonicalRelativePath>&& inputs,
    zc::Vector<identity::CanonicalRelativePath>&& outputs,
    zc::Vector<identity::SemanticEnvironmentName>&& environment,
    zc::Vector<identity::SemanticEnvironmentName>&& exportedEnvironment) {
  if (target.kind() != identity::CrateTargetKind::BuildScript) { return zc::none; }
  bool containsTarget = false;
  const auto targetPath = encodeValue(target.path());
  for (const auto& input : inputs) {
    if (encodeValue(input).asPtr() == targetPath.asPtr()) {
      containsTarget = true;
      break;
    }
  }
  if (!containsTarget) { inputs.add(target.path().clone()); }
  if (!sortEncodedUnique(inputs) || !sortEncodedUnique(outputs) ||
      !sortEncodedUnique(environment) || !sortEncodedUnique(exportedEnvironment)) {
    return zc::none;
  }
  return BuildScriptManifest(zc::mv(target), zc::mv(inputs), zc::mv(outputs), zc::mv(environment),
                             zc::mv(exportedEnvironment));
}

BuildScriptManifest BuildScriptManifest::clone() const {
  zc::Vector<identity::CanonicalRelativePath> inputs(inputValues.size());
  for (const auto& value : inputValues) { inputs.add(value.clone()); }
  zc::Vector<identity::CanonicalRelativePath> outputs(outputValues.size());
  for (const auto& value : outputValues) { outputs.add(value.clone()); }
  zc::Vector<identity::SemanticEnvironmentName> environment(environmentValues.size());
  for (const auto& value : environmentValues) { environment.add(value.clone()); }
  zc::Vector<identity::SemanticEnvironmentName> exported(exportedEnvironmentValues.size());
  for (const auto& value : exportedEnvironmentValues) { exported.add(value.clone()); }
  return BuildScriptManifest(targetValue.clone(), zc::mv(inputs), zc::mv(outputs),
                             zc::mv(environment), zc::mv(exported));
}

const TargetManifest& BuildScriptManifest::target() const noexcept { return targetValue; }
zc::ArrayPtr<const identity::CanonicalRelativePath> BuildScriptManifest::inputs() const noexcept {
  return inputValues.asPtr();
}
zc::ArrayPtr<const identity::CanonicalRelativePath> BuildScriptManifest::outputs() const noexcept {
  return outputValues.asPtr();
}
zc::ArrayPtr<const identity::SemanticEnvironmentName> BuildScriptManifest::environment()
    const noexcept {
  return environmentValues.asPtr();
}
zc::ArrayPtr<const identity::SemanticEnvironmentName> BuildScriptManifest::exportedEnvironment()
    const noexcept {
  return exportedEnvironmentValues.asPtr();
}

void BuildScriptManifest::encode(identity::CanonicalEncoder& encoder) const {
  targetValue.encode(encoder);
  encoder.encodeSequenceSize(inputValues.size());
  for (const auto& value : inputValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(outputValues.size());
  for (const auto& value : outputValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(environmentValues.size());
  for (const auto& value : environmentValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(exportedEnvironmentValues.size());
  for (const auto& value : exportedEnvironmentValues) { value.encode(encoder); }
}

CanonicalBuildScriptManifest::CanonicalBuildScriptManifest(
    CanonicalTargetManifest&& target, zc::Vector<identity::CanonicalRelativePath>&& inputs,
    zc::Vector<identity::CanonicalRelativePath>&& outputs,
    zc::Vector<identity::SemanticEnvironmentName>&& environment,
    zc::Vector<identity::SemanticEnvironmentName>&& exportedEnvironment) noexcept
    : targetValue(zc::mv(target)),
      inputValues(zc::mv(inputs)),
      outputValues(zc::mv(outputs)),
      environmentValues(zc::mv(environment)),
      exportedEnvironmentValues(zc::mv(exportedEnvironment)) {}

CanonicalBuildScriptManifest CanonicalBuildScriptManifest::from(const BuildScriptManifest& source) {
  zc::Vector<identity::CanonicalRelativePath> inputs(source.inputs().size());
  for (const auto& value : source.inputs()) { inputs.add(value.clone()); }
  zc::Vector<identity::CanonicalRelativePath> outputs(source.outputs().size());
  for (const auto& value : source.outputs()) { outputs.add(value.clone()); }
  zc::Vector<identity::SemanticEnvironmentName> environment(source.environment().size());
  for (const auto& value : source.environment()) { environment.add(value.clone()); }
  zc::Vector<identity::SemanticEnvironmentName> exported(source.exportedEnvironment().size());
  for (const auto& value : source.exportedEnvironment()) { exported.add(value.clone()); }
  return CanonicalBuildScriptManifest(CanonicalTargetManifest::from(source.target()),
                                      zc::mv(inputs), zc::mv(outputs), zc::mv(environment),
                                      zc::mv(exported));
}

CanonicalBuildScriptManifest CanonicalBuildScriptManifest::clone() const {
  zc::Vector<identity::CanonicalRelativePath> inputs(inputValues.size());
  for (const auto& value : inputValues) { inputs.add(value.clone()); }
  zc::Vector<identity::CanonicalRelativePath> outputs(outputValues.size());
  for (const auto& value : outputValues) { outputs.add(value.clone()); }
  zc::Vector<identity::SemanticEnvironmentName> environment(environmentValues.size());
  for (const auto& value : environmentValues) { environment.add(value.clone()); }
  zc::Vector<identity::SemanticEnvironmentName> exported(exportedEnvironmentValues.size());
  for (const auto& value : exportedEnvironmentValues) { exported.add(value.clone()); }
  return CanonicalBuildScriptManifest(targetValue.clone(), zc::mv(inputs), zc::mv(outputs),
                                      zc::mv(environment), zc::mv(exported));
}

CanonicalBuildScriptManifest CanonicalBuildScriptManifest::clone(
    zc::MemoryResource& resource) const {
  return CanonicalBuildScriptManifest(
      targetValue.clone(resource), cloneValues(resource, inputValues.asPtr()),
      cloneValues(resource, outputValues.asPtr()), cloneValues(resource, environmentValues.asPtr()),
      cloneValues(resource, exportedEnvironmentValues.asPtr()));
}

const CanonicalTargetManifest& CanonicalBuildScriptManifest::target() const noexcept {
  return targetValue;
}

zc::ArrayPtr<const identity::CanonicalRelativePath> CanonicalBuildScriptManifest::inputs()
    const noexcept {
  return inputValues;
}

zc::ArrayPtr<const identity::CanonicalRelativePath> CanonicalBuildScriptManifest::outputs()
    const noexcept {
  return outputValues;
}

zc::ArrayPtr<const identity::SemanticEnvironmentName> CanonicalBuildScriptManifest::environment()
    const noexcept {
  return environmentValues;
}

zc::ArrayPtr<const identity::SemanticEnvironmentName>
CanonicalBuildScriptManifest::exportedEnvironment() const noexcept {
  return exportedEnvironmentValues;
}

void CanonicalBuildScriptManifest::encode(identity::CanonicalEncoder& encoder) const {
  targetValue.encode(encoder);
  encoder.encodeSequenceSize(inputValues.size());
  for (const auto& value : inputValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(outputValues.size());
  for (const auto& value : outputValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(environmentValues.size());
  for (const auto& value : environmentValues) { value.encode(encoder); }
  encoder.encodeSequenceSize(exportedEnvironmentValues.size());
  for (const auto& value : exportedEnvironmentValues) { value.encode(encoder); }
}

zc::Array<uint8_t> CanonicalBuildScriptManifest::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

FeatureEdge::FeatureEdge(LocalFeatureEdge&& edge) noexcept : value(zc::mv(edge)) {}
FeatureEdge::FeatureEdge(EnableDependencyEdge&& edge) noexcept : value(zc::mv(edge)) {}
FeatureEdge::FeatureEdge(EnableDependencyFeatureEdge&& edge) noexcept : value(zc::mv(edge)) {}

FeatureEdge FeatureEdge::local(identity::FeatureName&& feature) {
  return FeatureEdge(LocalFeatureEdge{zc::mv(feature)});
}

FeatureEdge FeatureEdge::enableDependency(identity::DependencyAlias&& dependency) {
  return FeatureEdge(EnableDependencyEdge{zc::mv(dependency)});
}

FeatureEdge FeatureEdge::enableDependencyFeature(identity::DependencyAlias&& dependency,
                                                 identity::FeatureName&& feature) {
  return FeatureEdge(EnableDependencyFeatureEdge{zc::mv(dependency), zc::mv(feature)});
}

FeatureEdge FeatureEdge::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(edge, LocalFeatureEdge) { return local(edge.feature.clone()); }
    ZC_CASE_ONEOF(edge, EnableDependencyEdge) { return enableDependency(edge.dependency.clone()); }
    ZC_CASE_ONEOF(edge, EnableDependencyFeatureEdge) {
      return enableDependencyFeature(edge.dependency.clone(), edge.feature.clone());
    }
  }
  ZC_UNREACHABLE
}

FeatureEdge FeatureEdge::clone(zc::MemoryResource& resource) const {
  if (value.is<LocalFeatureEdge>()) {
    const auto& edge = value.get<LocalFeatureEdge>();
    return FeatureEdge(LocalFeatureEdge{edge.feature.clone(resource)});
  }
  if (value.is<EnableDependencyEdge>()) {
    const auto& edge = value.get<EnableDependencyEdge>();
    return FeatureEdge(EnableDependencyEdge{edge.dependency.clone(resource)});
  }
  const auto& edge = value.get<EnableDependencyFeatureEdge>();
  return FeatureEdge(
      EnableDependencyFeatureEdge{edge.dependency.clone(resource), edge.feature.clone(resource)});
}

FeatureEdgeKind FeatureEdge::kind() const noexcept {
  if (value.is<LocalFeatureEdge>()) { return FeatureEdgeKind::Local; }
  if (value.is<EnableDependencyEdge>()) { return FeatureEdgeKind::EnableDependency; }
  return FeatureEdgeKind::EnableDependencyFeature;
}

zc::StringPtr FeatureEdge::localFeature() const {
  ZC_IREQUIRE(kind() == FeatureEdgeKind::Local, "Local feature edge required.");
  return value.get<LocalFeatureEdge>().feature.text();
}

zc::StringPtr FeatureEdge::dependencyAlias() const {
  ZC_IREQUIRE(kind() != FeatureEdgeKind::Local, "Dependency feature edge required.");
  if (kind() == FeatureEdgeKind::EnableDependency) {
    return value.get<EnableDependencyEdge>().dependency.text();
  }
  return value.get<EnableDependencyFeatureEdge>().dependency.text();
}

zc::StringPtr FeatureEdge::dependencyFeature() const {
  ZC_IREQUIRE(kind() == FeatureEdgeKind::EnableDependencyFeature,
              "Dependency feature request edge required.");
  return value.get<EnableDependencyFeatureEdge>().feature.text();
}

void FeatureEdge::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(edge, LocalFeatureEdge) { edge.feature.encode(encoder); }
    ZC_CASE_ONEOF(edge, EnableDependencyEdge) { edge.dependency.encode(encoder); }
    ZC_CASE_ONEOF(edge, EnableDependencyFeatureEdge) {
      edge.dependency.encode(encoder);
      edge.feature.encode(encoder);
    }
  }
}

zc::Array<uint8_t> FeatureEdge::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

FeatureEdgeRecord::FeatureEdgeRecord(FeatureEdge&& edge, DiagnosticAnchor&& origin) noexcept
    : edgeValue(zc::mv(edge)), originValue(zc::mv(origin)) {}

FeatureEdgeRecord FeatureEdgeRecord::from(FeatureEdge&& edge, DiagnosticAnchor&& origin) {
  return FeatureEdgeRecord(zc::mv(edge), zc::mv(origin));
}

FeatureEdgeRecord FeatureEdgeRecord::clone() const {
  return FeatureEdgeRecord(edgeValue.clone(), originValue.clone());
}

const FeatureEdge& FeatureEdgeRecord::edge() const noexcept { return edgeValue; }

void FeatureEdgeRecord::encode(identity::CanonicalEncoder& encoder) const {
  edgeValue.encode(encoder);
  originValue.encode(encoder);
}

FeatureManifest::FeatureManifest(identity::FeatureName&& name,
                                 zc::Vector<FeatureEdgeRecord>&& edges) noexcept
    : nameValue(zc::mv(name)), edgeValues(zc::mv(edges)) {}

zc::Maybe<FeatureManifest> FeatureManifest::from(identity::FeatureName&& name,
                                                 zc::Vector<FeatureEdgeRecord>&& edges) {
  for (size_t index = 1; index < edges.size(); ++index) {
    auto current = zc::mv(edges[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           current.edge().encode().asPtr() < edges[insertion - 1].edge().encode().asPtr()) {
      edges[insertion] = zc::mv(edges[insertion - 1]);
      --insertion;
    }
    edges[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < edges.size(); ++index) {
    if (edges[index - 1].edge().encode().asPtr() == edges[index].edge().encode().asPtr()) {
      return zc::none;
    }
  }
  return FeatureManifest(zc::mv(name), zc::mv(edges));
}

FeatureManifest FeatureManifest::clone() const {
  zc::Vector<FeatureEdgeRecord> edges(edgeValues.size());
  for (const auto& edge : edgeValues) { edges.add(edge.clone()); }
  return FeatureManifest(nameValue.clone(), zc::mv(edges));
}

zc::StringPtr FeatureManifest::name() const noexcept { return nameValue.text(); }
zc::ArrayPtr<const FeatureEdgeRecord> FeatureManifest::edges() const noexcept {
  return edgeValues.asPtr();
}

void FeatureManifest::encode(identity::CanonicalEncoder& encoder) const {
  nameValue.encode(encoder);
  encoder.encodeSequenceSize(edgeValues.size());
  for (const auto& edge : edgeValues) { edge.encode(encoder); }
}

CanonicalFeatureManifest::CanonicalFeatureManifest(identity::FeatureName&& name,
                                                   zc::Vector<FeatureEdge>&& edges) noexcept
    : nameValue(zc::mv(name)), edgeValues(zc::mv(edges)) {}

CanonicalFeatureManifest CanonicalFeatureManifest::from(const FeatureManifest& source) {
  auto name = identity::FeatureName::fromCanonical(source.name());
  zc::Vector<FeatureEdge> edges(source.edges().size());
  for (const auto& record : source.edges()) { edges.add(record.edge().clone()); }
  ZC_IF_SOME(admitted, name) { return CanonicalFeatureManifest(zc::mv(admitted), zc::mv(edges)); }
  ZC_IREQUIRE(false, "normalized feature name must remain canonical");
  ZC_UNREACHABLE
}

CanonicalFeatureManifest CanonicalFeatureManifest::clone() const {
  zc::Vector<FeatureEdge> edges(edgeValues.size());
  for (const auto& edge : edgeValues) { edges.add(edge.clone()); }
  return CanonicalFeatureManifest(nameValue.clone(), zc::mv(edges));
}

CanonicalFeatureManifest CanonicalFeatureManifest::clone(zc::MemoryResource& resource) const {
  return CanonicalFeatureManifest(nameValue.clone(resource),
                                  cloneValues(resource, edgeValues.asPtr()));
}

zc::StringPtr CanonicalFeatureManifest::name() const noexcept { return nameValue.text(); }
zc::ArrayPtr<const FeatureEdge> CanonicalFeatureManifest::edges() const noexcept {
  return edgeValues.asPtr();
}

void CanonicalFeatureManifest::encode(identity::CanonicalEncoder& encoder) const {
  nameValue.encode(encoder);
  encoder.encodeSequenceSize(edgeValues.size());
  for (const auto& edge : edgeValues) { edge.encode(encoder); }
}

}  // namespace zomlang::compiler::driver::package
