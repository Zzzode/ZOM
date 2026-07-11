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

#include "zomlang/compiler/identity/source-key.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {

SourceOriginKey::SourceOriginKey(LocalFileSourceOrigin&& source) noexcept
    : value(zc::mv(source)) {}

SourceOriginKey::SourceOriginKey(RegistryFileSourceOrigin&& source) noexcept
    : value(zc::mv(source)) {}

SourceOriginKey::SourceOriginKey(VcsFileSourceOrigin&& source) noexcept
    : value(zc::mv(source)) {}

SourceOriginKey::SourceOriginKey(GeneratedFileSourceOrigin&& source) noexcept
    : value(zc::mv(source)) {}

SourceOriginKey SourceOriginKey::localFile(CanonicalWorkspaceRelativePath&& path) {
  return SourceOriginKey(LocalFileSourceOrigin{zc::mv(path)});
}

SourceOriginKey SourceOriginKey::registryFile(PackageKey&& package,
                                              CanonicalRelativePath&& path) {
  return SourceOriginKey(RegistryFileSourceOrigin{zc::mv(package), zc::mv(path)});
}

SourceOriginKey SourceOriginKey::vcsFile(PackageKey&& package, CanonicalRelativePath&& path) {
  return SourceOriginKey(VcsFileSourceOrigin{zc::mv(package), zc::mv(path)});
}

SourceOriginKey SourceOriginKey::generatedFile(BuildScriptOutputKey buildScriptOutput,
                                               CanonicalRelativePath&& logicalPath,
                                               const Sha256Digest& contentDigest) {
  return SourceOriginKey(GeneratedFileSourceOrigin{
      buildScriptOutput, zc::mv(logicalPath), contentDigest});
}

SourceOriginKey SourceOriginKey::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(source, LocalFileSourceOrigin) {
      return localFile(source.canonicalPath.clone());
    }
    ZC_CASE_ONEOF(source, RegistryFileSourceOrigin) {
      return registryFile(source.package.clone(), source.path.clone());
    }
    ZC_CASE_ONEOF(source, VcsFileSourceOrigin) {
      return vcsFile(source.package.clone(), source.path.clone());
    }
    ZC_CASE_ONEOF(source, GeneratedFileSourceOrigin) {
      return generatedFile(BuildScriptOutputKey::from(source.buildScriptOutput.digest()),
                           source.logicalPath.clone(), source.contentDigest);
    }
  }
  ZC_UNREACHABLE
}

SourceOriginKind SourceOriginKey::kind() const noexcept {
  if (value.is<LocalFileSourceOrigin>()) { return SourceOriginKind::LocalFile; }
  if (value.is<RegistryFileSourceOrigin>()) { return SourceOriginKind::RegistryFile; }
  if (value.is<VcsFileSourceOrigin>()) { return SourceOriginKind::VcsFile; }
  return SourceOriginKind::GeneratedFile;
}

bool SourceOriginKey::acceptsContentDigest(const Sha256Digest& contentDigest) const noexcept {
  ZC_IF_SOME(source, value.tryGet<GeneratedFileSourceOrigin>()) {
    return source.contentDigest == contentDigest;
  }
  return true;
}

void SourceOriginKey::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(source, LocalFileSourceOrigin) { source.canonicalPath.encode(encoder); }
    ZC_CASE_ONEOF(source, RegistryFileSourceOrigin) {
      source.package.encode(encoder);
      source.path.encode(encoder);
    }
    ZC_CASE_ONEOF(source, VcsFileSourceOrigin) {
      source.package.encode(encoder);
      source.path.encode(encoder);
    }
    ZC_CASE_ONEOF(source, GeneratedFileSourceOrigin) {
      source.buildScriptOutput.encode(encoder);
      source.logicalPath.encode(encoder);
      encoder.encodeDigest(source.contentDigest);
    }
  }
}

SourceFileKey::SourceFileKey(CrateKey&& crate, SourceOriginKey&& origin) noexcept
    : crateValue(zc::mv(crate)), originValue(zc::mv(origin)) {}

SourceFileKey SourceFileKey::from(CrateKey&& crate, SourceOriginKey&& origin) {
  return SourceFileKey(zc::mv(crate), zc::mv(origin));
}

SourceFileKey SourceFileKey::clone() const {
  return SourceFileKey(crateValue.clone(), originValue.clone());
}

bool SourceFileKey::sameAs(const SourceFileKey& other) const {
  auto left = encode();
  auto right = other.encode();
  return left.asPtr() == right.asPtr();
}

bool SourceFileKey::belongsTo(const CrateKey& crate) const {
  auto owner = crateValue.encode();
  auto candidate = crate.encode();
  return owner.asPtr() == candidate.asPtr();
}

bool SourceFileKey::acceptsContentDigest(const Sha256Digest& contentDigest) const noexcept {
  return originValue.acceptsContentDigest(contentDigest);
}

void SourceFileKey::encode(CanonicalEncoder& encoder) const {
  crateValue.encode(encoder);
  originValue.encode(encoder);
}

zc::Array<uint8_t> SourceFileKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

SourceSpan::SourceSpan(SourceFileKey&& source, uint64_t byteStart, uint64_t byteEnd) noexcept
    : sourceValue(zc::mv(source)), startValue(byteStart), endValue(byteEnd) {}

SourceSpan SourceSpan::clone() const { return SourceSpan(sourceValue.clone(), startValue, endValue); }

bool SourceSpan::belongsTo(const SourceFileKey& source) const { return sourceValue.sameAs(source); }

void SourceSpan::encode(CanonicalEncoder& encoder) const {
  sourceValue.encode(encoder);
  encoder.encodeUint64(startValue);
  encoder.encodeUint64(endValue);
}

ModuleKey::ModuleKey(CrateKey&& crate, zc::Vector<ModulePathSegment>&& canonicalPath,
                     SourceFileKey&& source,
                     zc::Maybe<SourceSpan>&& declarationAnchor) noexcept
    : crateValue(zc::mv(crate)),
      pathValue(zc::mv(canonicalPath)),
      sourceValue(zc::mv(source)),
      declarationAnchorValue(zc::mv(declarationAnchor)) {}

zc::Maybe<ModuleKey> ModuleKey::from(CrateKey&& crate,
                                     zc::Vector<ModulePathSegment>&& canonicalPath,
                                     SourceFileKey&& source,
                                     zc::Maybe<SourceSpan>&& declarationAnchor) {
  if (canonicalPath.size() == 0 || !source.belongsTo(crate)) { return zc::none; }
  ZC_IF_SOME(anchor, declarationAnchor) {
    if (!anchor.belongsTo(source)) { return zc::none; }
  }
  return ModuleKey(zc::mv(crate), zc::mv(canonicalPath), zc::mv(source),
                   zc::mv(declarationAnchor));
}

ModuleKey ModuleKey::clone() const {
  zc::Vector<ModulePathSegment> path(pathValue.size());
  for (const auto& segment : pathValue) { path.add(segment.clone()); }
  zc::Maybe<SourceSpan> anchor;
  ZC_IF_SOME(value, declarationAnchorValue) { anchor = value.clone(); }
  return ModuleKey(crateValue.clone(), zc::mv(path), sourceValue.clone(), zc::mv(anchor));
}

bool ModuleKey::contains(const SourceSpan& span) const { return span.belongsTo(sourceValue); }

void ModuleKey::encode(CanonicalEncoder& encoder) const {
  crateValue.encode(encoder);
  encoder.encodeSequenceSize(pathValue.size());
  for (const auto& segment : pathValue) { segment.encode(encoder); }
  sourceValue.encode(encoder);
  ZC_IF_SOME(anchor, declarationAnchorValue) {
    encoder.encodeSome();
    anchor.encode(encoder);
  }
  else {
    encoder.encodeNone();
  }
}

zc::Array<uint8_t> ModuleKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
