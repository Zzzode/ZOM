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

#include "compiler/driver/package/source-tree.h"

#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/text/unicode-normalization.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Array<uint8_t> encodePath(const identity::CanonicalRelativePath& path) {
  identity::CanonicalEncoder encoder;
  path.encode(encoder);
  return encoder.finish();
}

bool encodedLess(const SourceTreeFile& left, const SourceTreeFile& right) {
  return encodePath(left.path()).asPtr() < encodePath(right.path()).asPtr();
}

zc::Maybe<identity::Sha256Digest> computeTreeDigest(zc::ArrayPtr<const SourceTreeFile> files) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(files.size());
  for (const auto& file : files) { file.encode(encoder); }
  auto encoded = encoder.finish();

  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  if (!hasher.update("zom.source-tree"_zc.asBytes()) ||
      !hasher.update(zc::arrayPtr(&separator, 1)) || !hasher.update(encoded.asPtr())) {
    return zc::none;
  }
  return hasher.finish();
}

struct AdmittedPath final {
  zc::String raw;
  identity::CanonicalRelativePath canonical;
  zc::Array<uint8_t> encoded;
  zc::String folded;
};

using PathAdmissionResult = zc::OneOf<AdmittedPath, MaterializationIssue>;

PathAdmissionResult admitPath(zc::StringPtr raw) {
  if (raw.size() == 0) { return MaterializationIssue::EmptySegment; }
  if (raw.size() > 4096) { return MaterializationIssue::PathTooLong; }
  if (raw[0] == '/') { return MaterializationIssue::AbsolutePath; }

  zc::Vector<identity::CanonicalPathSegment> segments;
  size_t segmentStart = 0;
  size_t canonicalLength = 0;
  for (size_t index = 0; index <= raw.size(); ++index) {
    if (index < raw.size() && raw[index] == '\\') { return MaterializationIssue::BackslashPath; }
    if (index < raw.size() && raw[index] != '/') { continue; }
    if (index == segmentStart) { return MaterializationIssue::EmptySegment; }
    const auto sourceSegmentStorage = zc::heapString(raw.slice(segmentStart, index));
    const zc::StringPtr sourceSegment(sourceSegmentStorage);
    if (sourceSegment == "."_zc) { return MaterializationIssue::DotPath; }
    if (sourceSegment == ".."_zc) { return MaterializationIssue::ParentPath; }
    auto segment = identity::CanonicalPathSegment::fromSource(sourceSegment);
    if (segment == zc::none) { return MaterializationIssue::InvalidEntryEncoding; }
    ZC_IF_SOME(value, segment) {
      if (segments.size() != 0) { ++canonicalLength; }
      if (canonicalLength > 4096 - value.text().size()) {
        return MaterializationIssue::PathTooLong;
      }
      canonicalLength += value.text().size();
      segments.add(zc::mv(value));
    }
    if (segments.size() > 128) { return MaterializationIssue::PathTooDeep; }
    segmentStart = index + 1;
  }

  zc::Vector<char> foldedPath;
  for (size_t index = 0; index < segments.size(); ++index) {
    if (index != 0) { foldedPath.add('/'); }
    auto folded = identity::fullCaseFold(segments[index].text());
    if (folded == zc::none) { return MaterializationIssue::InvalidEntryEncoding; }
    ZC_IF_SOME(value, folded) { foldedPath.addAll(value); }
  }
  auto canonical = identity::CanonicalRelativePath::from(zc::mv(segments));
  auto encoded = encodePath(canonical);
  return AdmittedPath{zc::str(raw), zc::mv(canonical), zc::mv(encoded),
                      zc::str(foldedPath.releaseAsArray())};
}

struct PriorPath final {
  zc::String raw;
  zc::Array<uint8_t> encoded;
  zc::String folded;
};

struct ActiveFile final {
  ActiveFile(identity::CanonicalRelativePath&& path, uint64_t expectedLength) noexcept
      : path(zc::mv(path)), expectedLength(expectedLength) {}

  identity::CanonicalRelativePath path;
  uint64_t expectedLength;
  uint64_t writtenLength = 0;
  identity::Sha256Hasher hasher;
};

}  // namespace

SourceTreeFile::SourceTreeFile(identity::CanonicalRelativePath&& path, uint64_t byteLength,
                               const identity::Sha256Digest& contentDigest) noexcept
    : pathValue(zc::mv(path)), byteLengthValue(byteLength), contentDigestValue(contentDigest) {}

SourceTreeFile SourceTreeFile::from(identity::CanonicalRelativePath&& path, uint64_t byteLength,
                                    const identity::Sha256Digest& contentDigest) {
  return SourceTreeFile(zc::mv(path), byteLength, contentDigest);
}

SourceTreeFile SourceTreeFile::clone() const {
  return SourceTreeFile(pathValue.clone(), byteLengthValue, contentDigestValue);
}

const identity::CanonicalRelativePath& SourceTreeFile::path() const noexcept { return pathValue; }
uint64_t SourceTreeFile::byteLength() const noexcept { return byteLengthValue; }
const identity::Sha256Digest& SourceTreeFile::contentDigest() const noexcept {
  return contentDigestValue;
}

void SourceTreeFile::encode(identity::CanonicalEncoder& encoder) const {
  pathValue.encode(encoder);
  encoder.encodeUint64(byteLengthValue);
  encoder.encodeDigest(contentDigestValue);
}

zc::Array<uint8_t> SourceTreeFile::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

SourceTreeRecord::SourceTreeRecord(zc::Vector<SourceTreeFile>&& files,
                                   const identity::Sha256Digest& digest) noexcept
    : fileValues(zc::mv(files)), digestValue(digest) {}

zc::Maybe<SourceTreeRecord> SourceTreeRecord::from(zc::Vector<SourceTreeFile>&& files) {
  for (size_t index = 1; index < files.size(); ++index) {
    auto current = zc::mv(files[index]);
    size_t insertion = index;
    while (insertion != 0 && encodedLess(current, files[insertion - 1])) {
      files[insertion] = zc::mv(files[insertion - 1]);
      --insertion;
    }
    files[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < files.size(); ++index) {
    if (encodePath(files[index - 1].path()).asPtr() == encodePath(files[index].path()).asPtr()) {
      return zc::none;
    }
  }
  ZC_IF_SOME(digest, computeTreeDigest(files)) { return SourceTreeRecord(zc::mv(files), digest); }
  return zc::none;
}

SourceTreeRecord SourceTreeRecord::clone() const {
  zc::Vector<SourceTreeFile> files;
  for (const auto& file : fileValues) { files.add(file.clone()); }
  return SourceTreeRecord(zc::mv(files), digestValue);
}

zc::ArrayPtr<const SourceTreeFile> SourceTreeRecord::files() const noexcept { return fileValues; }
const identity::Sha256Digest& SourceTreeRecord::digest() const noexcept { return digestValue; }

struct SourceTreeBuilder::Impl final {
  zc::Vector<PriorPath> paths;
  zc::Vector<SourceTreeFile> files;
  zc::Maybe<ActiveFile> active;
  bool finished = false;
  zc::Maybe<MaterializationIssue> issue;
};

SourceTreeBuilder::SourceTreeBuilder() : impl(zc::heap<Impl>()) {}
SourceTreeBuilder::~SourceTreeBuilder() noexcept(false) = default;
SourceTreeBuilder::SourceTreeBuilder(SourceTreeBuilder&&) noexcept = default;
SourceTreeBuilder& SourceTreeBuilder::operator=(SourceTreeBuilder&&) noexcept = default;

zc::Maybe<MaterializationIssue> SourceTreeBuilder::beginFile(zc::StringPtr path,
                                                             uint64_t byteLength) {
  if (impl->finished || impl->active != zc::none || impl->issue != zc::none) {
    return MaterializationIssue::ArchiveDecodeFailed;
  }
  auto admitted = admitPath(path);
  if (admitted.is<MaterializationIssue>()) {
    impl->issue = admitted.get<MaterializationIssue>();
    return admitted.get<MaterializationIssue>();
  }
  auto value = zc::mv(admitted.get<AdmittedPath>());
  for (const auto& prior : impl->paths) {
    MaterializationIssue collision;
    if (prior.raw == value.raw) {
      collision = MaterializationIssue::DuplicatePath;
    } else if (prior.encoded.asPtr() == value.encoded.asPtr()) {
      collision = MaterializationIssue::UnicodeCollision;
    } else if (prior.folded == value.folded) {
      collision = MaterializationIssue::CaseFoldCollision;
    } else {
      continue;
    }
    impl->issue = collision;
    return collision;
  }
  impl->paths.add(PriorPath{zc::mv(value.raw), zc::mv(value.encoded), zc::mv(value.folded)});
  impl->active = ActiveFile(zc::mv(value.canonical), byteLength);
  return zc::none;
}

zc::Maybe<MaterializationIssue> SourceTreeBuilder::write(zc::ArrayPtr<const zc::byte> bytes) {
  if (impl->finished || impl->issue != zc::none || impl->active == zc::none) {
    return MaterializationIssue::ArchiveDecodeFailed;
  }
  ZC_IF_SOME(active, impl->active) {
    if (active.writtenLength > UINT64_MAX - bytes.size()) {
      impl->issue = MaterializationIssue::LengthOverflow;
      return MaterializationIssue::LengthOverflow;
    }
    active.writtenLength += bytes.size();
    if (active.writtenLength > active.expectedLength || !active.hasher.update(bytes)) {
      impl->issue = MaterializationIssue::ArchiveDecodeFailed;
      return MaterializationIssue::ArchiveDecodeFailed;
    }
  }
  return zc::none;
}

zc::Maybe<MaterializationIssue> SourceTreeBuilder::endFile() {
  if (impl->finished || impl->issue != zc::none || impl->active == zc::none) {
    return MaterializationIssue::ArchiveDecodeFailed;
  }
  ZC_IF_SOME(active, impl->active) {
    if (active.writtenLength != active.expectedLength) {
      impl->issue = MaterializationIssue::ArchiveDecodeFailed;
      return MaterializationIssue::ArchiveDecodeFailed;
    }
    auto digest = active.hasher.finish();
    if (digest == zc::none) {
      impl->issue = MaterializationIssue::LengthOverflow;
      return MaterializationIssue::LengthOverflow;
    }
    ZC_IF_SOME(value, digest) {
      impl->files.add(SourceTreeFile::from(zc::mv(active.path), active.expectedLength, value));
    }
  }
  impl->active = zc::none;
  return zc::none;
}

SourceTreeBuildResult SourceTreeBuilder::finish() {
  if (impl->finished || impl->active != zc::none) {
    return MaterializationIssue::ArchiveDecodeFailed;
  }
  impl->finished = true;
  ZC_IF_SOME(issue, impl->issue) { return issue; }
  auto record = SourceTreeRecord::from(zc::mv(impl->files));
  ZC_IF_SOME(value, record) { return zc::mv(value); }
  return MaterializationIssue::SourceTreeDigestMismatch;
}

}  // namespace zomlang::compiler::driver::package
