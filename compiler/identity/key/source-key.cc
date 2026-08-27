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

#include "compiler/identity/key/source-key.h"

#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

constexpr uint64_t kMaximumModulePathSegments = 256;
constexpr uint64_t kMaximumModuleKeyBytes = 16 * 1024;

}  // namespace

SourceOriginKey::SourceOriginKey(LocalFileSourceOrigin&& source) noexcept : value(zc::mv(source)) {}

SourceOriginKey::SourceOriginKey(RegistryFileSourceOrigin&& source) noexcept
    : value(zc::mv(source)) {}

SourceOriginKey::SourceOriginKey(VcsFileSourceOrigin&& source) noexcept : value(zc::mv(source)) {}

SourceOriginKey::SourceOriginKey(GeneratedFileSourceOrigin&& source) noexcept
    : value(zc::mv(source)) {}

SourceOriginKey::SourceOriginKey(CoreFileSourceOrigin&& source) noexcept : value(zc::mv(source)) {}

SourceOriginKey SourceOriginKey::localFile(CanonicalWorkspaceRelativePath&& path) {
  return SourceOriginKey(LocalFileSourceOrigin{zc::mv(path)});
}

SourceOriginKey SourceOriginKey::registryFile(PackageKey&& package, CanonicalRelativePath&& path) {
  return SourceOriginKey(RegistryFileSourceOrigin{zc::mv(package), zc::mv(path)});
}

SourceOriginKey SourceOriginKey::vcsFile(PackageKey&& package, CanonicalRelativePath&& path) {
  return SourceOriginKey(VcsFileSourceOrigin{zc::mv(package), zc::mv(path)});
}

SourceOriginKey SourceOriginKey::generatedFile(BuildScriptProducerKey producer,
                                               CanonicalRelativePath&& logicalPath) {
  return SourceOriginKey(GeneratedFileSourceOrigin{producer, zc::mv(logicalPath)});
}

SourceOriginKey SourceOriginKey::coreFile(ToolchainUnitKey toolchain,
                                          CanonicalRelativePath&& path) {
  return SourceOriginKey(CoreFileSourceOrigin{toolchain, zc::mv(path)});
}

zc::Maybe<SourceOriginKey> SourceOriginKey::decodeCanonical(CanonicalDecoder& decoder) {
  auto kind = decoder.decodeUint8();
  ZC_IF_SOME(tag, kind) {
    switch (static_cast<SourceOriginKind>(tag)) {
      case SourceOriginKind::LocalFile: {
        auto path = CanonicalWorkspaceRelativePath::decodeCanonical(decoder);
        ZC_IF_SOME(value, path) { return SourceOriginKey::localFile(zc::mv(value)); }
        return zc::none;
      }
      case SourceOriginKind::RegistryFile: {
        auto package = PackageKey::decodeCanonical(decoder);
        if (package == zc::none) { return zc::none; }
        auto path = CanonicalRelativePath::decodeCanonical(decoder);
        if (path == zc::none) { return zc::none; }
        ZC_IF_SOME(packageValue, package) {
          ZC_IF_SOME(pathValue, path) {
            return SourceOriginKey::registryFile(zc::mv(packageValue), zc::mv(pathValue));
          }
        }
        return zc::none;
      }
      case SourceOriginKind::VcsFile: {
        auto package = PackageKey::decodeCanonical(decoder);
        if (package == zc::none) { return zc::none; }
        auto path = CanonicalRelativePath::decodeCanonical(decoder);
        if (path == zc::none) { return zc::none; }
        ZC_IF_SOME(packageValue, package) {
          ZC_IF_SOME(pathValue, path) {
            return SourceOriginKey::vcsFile(zc::mv(packageValue), zc::mv(pathValue));
          }
        }
        return zc::none;
      }
      case SourceOriginKind::GeneratedFile: {
        auto producer = BuildScriptProducerKey::decodeCanonical(decoder);
        if (producer == zc::none) { return zc::none; }
        auto logicalPath = CanonicalRelativePath::decodeCanonical(decoder);
        if (logicalPath == zc::none) { return zc::none; }
        ZC_IF_SOME(producerValue, producer) {
          ZC_IF_SOME(pathValue, logicalPath) {
            return SourceOriginKey::generatedFile(producerValue, zc::mv(pathValue));
          }
        }
        return zc::none;
      }
      case SourceOriginKind::CoreFile: {
        auto toolchain = ToolchainUnitKey::decodeCanonical(decoder);
        if (toolchain == zc::none) { return zc::none; }
        auto path = CanonicalRelativePath::decodeCanonical(decoder);
        if (path == zc::none) { return zc::none; }
        ZC_IF_SOME(toolchainValue, toolchain) {
          ZC_IF_SOME(pathValue, path) {
            return SourceOriginKey::coreFile(toolchainValue, zc::mv(pathValue));
          }
        }
        return zc::none;
      }
    }
  }
  return zc::none;
}

SourceOriginKey SourceOriginKey::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(source, LocalFileSourceOrigin) { return localFile(source.canonicalPath.clone()); }
    ZC_CASE_ONEOF(source, RegistryFileSourceOrigin) {
      return registryFile(source.package.clone(), source.path.clone());
    }
    ZC_CASE_ONEOF(source, VcsFileSourceOrigin) {
      return vcsFile(source.package.clone(), source.path.clone());
    }
    ZC_CASE_ONEOF(source, GeneratedFileSourceOrigin) {
      return generatedFile(BuildScriptProducerKey::from(source.producer.digest()),
                           source.logicalPath.clone());
    }
    ZC_CASE_ONEOF(source, CoreFileSourceOrigin) {
      return coreFile(ToolchainUnitKey::core(), source.path.clone());
    }
  }
  ZC_UNREACHABLE
}

SourceOriginKind SourceOriginKey::kind() const noexcept {
  if (value.is<LocalFileSourceOrigin>()) { return SourceOriginKind::LocalFile; }
  if (value.is<RegistryFileSourceOrigin>()) { return SourceOriginKind::RegistryFile; }
  if (value.is<VcsFileSourceOrigin>()) { return SourceOriginKind::VcsFile; }
  if (value.is<GeneratedFileSourceOrigin>()) { return SourceOriginKind::GeneratedFile; }
  return SourceOriginKind::CoreFile;
}

zc::Maybe<zc::String> SourceOriginKey::logicalFileName() const {
  zc::ArrayPtr<const CanonicalPathSegment> segments;
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(source, LocalFileSourceOrigin) { segments = source.canonicalPath.segments(); }
    ZC_CASE_ONEOF(source, RegistryFileSourceOrigin) { segments = source.path.segments(); }
    ZC_CASE_ONEOF(source, VcsFileSourceOrigin) { segments = source.path.segments(); }
    ZC_CASE_ONEOF(source, GeneratedFileSourceOrigin) { segments = source.logicalPath.segments(); }
    ZC_CASE_ONEOF(source, CoreFileSourceOrigin) { segments = source.path.segments(); }
  }
  if (segments.size() == 0) { return zc::none; }
  return zc::str(segments.back().text());
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
      source.producer.encode(encoder);
      source.logicalPath.encode(encoder);
    }
    ZC_CASE_ONEOF(source, CoreFileSourceOrigin) {
      source.toolchain.encode(encoder);
      source.path.encode(encoder);
    }
  }
}

SourceFileKey::SourceFileKey(CrateKey&& crate, SourceOriginKey&& origin) noexcept
    : crateValue(zc::mv(crate)), originValue(zc::mv(origin)) {}

SourceFileKey SourceFileKey::from(CrateKey&& crate, SourceOriginKey&& origin) {
  return SourceFileKey(zc::mv(crate), zc::mv(origin));
}

zc::Maybe<SourceFileKey> SourceFileKey::decodeCanonical(CanonicalDecoder& decoder) {
  auto crate = CrateKey::decodeCanonical(decoder);
  if (crate == zc::none) { return zc::none; }
  auto origin = SourceOriginKey::decodeCanonical(decoder);
  if (origin == zc::none) { return zc::none; }
  ZC_IF_SOME(crateValue, crate) {
    ZC_IF_SOME(originValue, origin) {
      return SourceFileKey(zc::mv(crateValue), zc::mv(originValue));
    }
  }
  return zc::none;
}

SourceFileKey SourceFileKey::clone() const {
  return SourceFileKey(crateValue.clone(), originValue.clone());
}

const CrateKey& SourceFileKey::crate() const noexcept { return crateValue; }
const SourceOriginKey& SourceFileKey::origin() const noexcept { return originValue; }

zc::Maybe<zc::String> SourceFileKey::logicalFileName() const {
  return originValue.logicalFileName();
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

SourceSpan SourceSpan::clone() const {
  return SourceSpan(sourceValue.clone(), startValue, endValue);
}

const SourceFileKey& SourceSpan::source() const noexcept { return sourceValue; }

bool SourceSpan::belongsTo(const SourceFileKey& source) const { return sourceValue.sameAs(source); }

uint64_t SourceSpan::byteStart() const noexcept { return startValue; }

uint64_t SourceSpan::byteEnd() const noexcept { return endValue; }

void SourceSpan::encode(CanonicalEncoder& encoder) const {
  sourceValue.encode(encoder);
  encoder.encodeUint64(startValue);
  encoder.encodeUint64(endValue);
}

ModuleKey::ModuleKey(CrateKey&& crate, zc::Vector<ModulePathSegment>&& canonicalPath) noexcept
    : crateValue(zc::mv(crate)), pathValue(zc::mv(canonicalPath)) {}

zc::Maybe<ModuleKey> ModuleKey::from(CrateKey&& crate,
                                     zc::Vector<ModulePathSegment>&& canonicalPath) {
  if (canonicalPath.size() == 0 || canonicalPath.size() > kMaximumModulePathSegments) {
    return zc::none;
  }
  ModuleKey candidate(zc::mv(crate), zc::mv(canonicalPath));
  if (candidate.encode().size() > kMaximumModuleKeyBytes) { return zc::none; }
  return zc::mv(candidate);
}

zc::Maybe<ModuleKey> ModuleKey::decodeCanonical(CanonicalDecoder& decoder) {
  const uint64_t initialRemaining = decoder.remaining();
  auto crate = CrateKey::decodeCanonical(decoder);
  if (crate == zc::none) { return zc::none; }
  auto pathSize = decoder.decodeSequenceSize(kMaximumModulePathSegments);
  if (pathSize == zc::none) { return zc::none; }
  ZC_IF_SOME(crateValue, crate) {
    ZC_IF_SOME(size, pathSize) {
      if (size == 0) { return zc::none; }
      zc::Vector<ModulePathSegment> path(static_cast<size_t>(size));
      for (uint64_t index = 0; index < size; ++index) {
        auto segment = ModulePathSegment::decodeCanonical(decoder);
        if (segment == zc::none) { return zc::none; }
        ZC_IF_SOME(value, segment) { path.add(zc::mv(value)); }
      }
      if (initialRemaining - decoder.remaining() > kMaximumModuleKeyBytes) { return zc::none; }
      return ModuleKey(zc::mv(crateValue), zc::mv(path));
    }
  }
  return zc::none;
}

ModuleKey ModuleKey::clone() const {
  zc::Vector<ModulePathSegment> path(pathValue.size());
  for (const auto& segment : pathValue) { path.add(segment.clone()); }
  return ModuleKey(crateValue.clone(), zc::mv(path));
}

const CrateKey& ModuleKey::crate() const noexcept { return crateValue; }

zc::ArrayPtr<const ModulePathSegment> ModuleKey::path() const noexcept { return pathValue.asPtr(); }

void ModuleKey::encode(CanonicalEncoder& encoder) const {
  crateValue.encode(encoder);
  encoder.encodeSequenceSize(pathValue.size());
  for (const auto& segment : pathValue) { segment.encode(encoder); }
}

zc::Array<uint8_t> ModuleKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
