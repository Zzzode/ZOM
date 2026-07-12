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

#include "zomlang/compiler/driver/package/lockfile.h"

#include "zc/core/encoding.h"
#include "zc/core/exception.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

template <typename Value>
zc::Vector<Value> canonicalSort(zc::Vector<Value>&& input) {
  if (input.size() < 2) { return zc::mv(input); }
  const size_t middle = input.size() / 2;
  zc::Vector<Value> left;
  zc::Vector<Value> right;
  for (size_t index = 0; index < input.size(); ++index) {
    if (index < middle) {
      left.add(zc::mv(input[index]));
    } else {
      right.add(zc::mv(input[index]));
    }
  }
  left = canonicalSort(zc::mv(left));
  right = canonicalSort(zc::mv(right));
  zc::Vector<Value> result;
  size_t leftIndex = 0;
  size_t rightIndex = 0;
  while (leftIndex < left.size() || rightIndex < right.size()) {
    bool takeLeft = rightIndex == right.size();
    if (!takeLeft && leftIndex < left.size()) {
      identity::CanonicalEncoder leftEncoder;
      identity::CanonicalEncoder rightEncoder;
      left[leftIndex].encode(leftEncoder);
      right[rightIndex].encode(rightEncoder);
      takeLeft = leftEncoder.finish().asPtr() < rightEncoder.finish().asPtr();
    }
    if (takeLeft) {
      result.add(zc::mv(left[leftIndex++]));
    } else {
      result.add(zc::mv(right[rightIndex++]));
    }
  }
  return result;
}

zc::Array<uint8_t> encodePackageKey(const identity::PackageKey& key) { return key.encode(); }

bool containsPackage(zc::ArrayPtr<const LockPackageRecord> packages,
                     const identity::PackageKey& key) {
  const auto encoded = key.encode();
  size_t first = 0;
  size_t count = packages.size();
  while (count != 0) {
    const size_t step = count / 2;
    const size_t middle = first + step;
    const auto candidate = packages[middle].key().encode();
    if (candidate.asPtr() < encoded.asPtr()) {
      first = middle + 1;
      count -= step + 1;
      continue;
    }
    if (candidate.asPtr() == encoded.asPtr()) { return true; }
    count = step;
  }
  return false;
}

zc::Maybe<ArchiveFormat> cloneArchiveFormat(const zc::Maybe<ArchiveFormat>& value) {
  ZC_IF_SOME(item, value) { return item; }
  return zc::none;
}

zc::Maybe<identity::Sha256Digest> cloneDigest(const zc::Maybe<identity::Sha256Digest>& value) {
  ZC_IF_SOME(item, value) { return item; }
  return zc::none;
}

zc::Maybe<SigningKeyId> cloneSigningKey(const zc::Maybe<SigningKeyId>& value) {
  ZC_IF_SOME(item, value) { return item; }
  return zc::none;
}

void append(zc::Vector<char>& output, zc::StringPtr text) { output.addAll(text); }

char hexDigit(uint8_t value) {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

void appendTomlString(zc::Vector<char>& output, zc::StringPtr text) {
  output.add('"');
  for (const char byte : text) {
    switch (byte) {
      case '\b':
        append(output, "\\b"_zc);
        break;
      case '\t':
        append(output, "\\t"_zc);
        break;
      case '\n':
        append(output, "\\n"_zc);
        break;
      case '\f':
        append(output, "\\f"_zc);
        break;
      case '\r':
        append(output, "\\r"_zc);
        break;
      case '"':
        append(output, "\\\""_zc);
        break;
      case '\\':
        append(output, "\\\\"_zc);
        break;
      default: {
        const auto value = static_cast<uint8_t>(byte);
        if (value < 0x20U || value == 0x7fU) {
          append(output, "\\u00"_zc);
          output.add(hexDigit(static_cast<uint8_t>(value >> 4U)));
          output.add(hexDigit(static_cast<uint8_t>(value & 0x0fU)));
        } else {
          output.add(byte);
        }
        break;
      }
    }
  }
  output.add('"');
}

void appendField(zc::Vector<char>& output, zc::StringPtr name, zc::StringPtr value) {
  append(output, name);
  append(output, " = "_zc);
  appendTomlString(output, value);
  output.add('\n');
}

zc::StringPtr sourceKindName(identity::PackageSourceKind kind) {
  switch (kind) {
    case identity::PackageSourceKind::Registry:
      return "registry"_zc;
    case identity::PackageSourceKind::Vcs:
      return "vcs"_zc;
    case identity::PackageSourceKind::LocalPath:
      return "local"_zc;
  }
  ZC_UNREACHABLE
}

zc::StringPtr dependencyDomainName(identity::DependencyDomain domain) {
  switch (domain) {
    case identity::DependencyDomain::Target:
      return "target"_zc;
    case identity::DependencyDomain::Development:
      return "development"_zc;
    case identity::DependencyDomain::Build:
      return "build"_zc;
  }
  ZC_UNREACHABLE
}

void appendFeatures(zc::Vector<char>& output, zc::ArrayPtr<const identity::FeatureName> features) {
  append(output, "features = ["_zc);
  for (size_t index = 0; index < features.size(); ++index) {
    if (index != 0) { append(output, ", "_zc); }
    appendTomlString(output, features[index].text());
  }
  append(output, "]\n"_zc);
}

bool sameInjected(zc::Maybe<LockWriteStage> injected, LockWriteStage stage) {
  ZC_IF_SOME(value, injected) { return value == stage; }
  return false;
}

zc::Maybe<zc::Array<uint8_t>> decodeLowerHex(zc::StringPtr text) {
  if (text.size() == 0 || text.size() % 2 != 0) { return zc::none; }
  for (char byte : text) {
    if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) { return zc::none; }
  }
  auto decoded = zc::decodeHex(text);
  if (decoded == zc::none) { return zc::none; }
  zc::Vector<uint8_t> result(decoded.size());
  for (zc::byte byte : decoded) { result.add(static_cast<uint8_t>(byte)); }
  return result.releaseAsArray();
}

zc::Maybe<identity::Sha256Digest> parseDigest(zc::StringPtr text) {
  auto bytes = decodeLowerHex(text);
  ZC_IF_SOME(value, bytes) { return identity::Sha256Digest::fromBytes(value); }
  return zc::none;
}

using SourceDecodeResult = zc::OneOf<identity::CanonicalPackageSource, LockIssue>;

zc::Maybe<zc::String> decodeText(identity::CanonicalDecoder& decoder, uint64_t maximumBytes) {
  auto bytes = decoder.decodeByteString(maximumBytes);
  ZC_IF_SOME(value, bytes) {
    auto validated = zc::encodeUtf32(value.asChars());
    if (validated != zc::none) { return zc::heapString(value.asChars()); }
  }
  return zc::none;
}

zc::Maybe<zc::Vector<identity::CanonicalPathSegment>> decodeSegments(
    identity::CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(128);
  ZC_IF_SOME(countValue, count) {
    zc::Vector<identity::CanonicalPathSegment> segments;
    for (uint64_t index = 0; index < countValue; ++index) {
      auto text = decodeText(decoder, 4096);
      ZC_IF_SOME(textValue, text) {
        auto segment = identity::CanonicalPathSegment::fromCanonical(textValue);
        ZC_IF_SOME(segmentValue, segment) {
          segments.add(zc::mv(segmentValue));
          continue;
        }
      }
      return zc::none;
    }
    return zc::mv(segments);
  }
  return zc::none;
}

SourceDecodeResult decodeSource(identity::CanonicalDecoder& decoder) {
  auto tag = decoder.decodeUint8();
  ZC_IF_SOME(tagValue, tag) {
    if (tagValue == static_cast<uint8_t>(identity::PackageSourceKind::Registry)) {
      auto urlText = decodeText(decoder, 4096);
      auto trust = decoder.decodeDigest();
      ZC_IF_SOME(urlValue, urlText) {
        ZC_IF_SOME(trustValue, trust) {
          auto canonicalUrl = identity::CanonicalUrl::fromCanonical(urlValue);
          ZC_IF_SOME(admittedUrl, canonicalUrl) {
            return identity::CanonicalPackageSource::registry(
                identity::RegistryIdentity::from(zc::mv(admittedUrl), trustValue));
          }
        }
      }
      return LockIssue::SourceKeyMismatch;
    }
    if (tagValue == static_cast<uint8_t>(identity::PackageSourceKind::Vcs)) {
      auto urlText = decodeText(decoder, 4096);
      auto algorithm = decoder.decodeUint8();
      ZC_IF_SOME(urlValue, urlText) {
        ZC_IF_SOME(algorithmValue, algorithm) {
          size_t digestLength = 0;
          if (algorithmValue == static_cast<uint8_t>(identity::VcsRevisionAlgorithm::Sha1)) {
            digestLength = 20;
          } else if (algorithmValue ==
                     static_cast<uint8_t>(identity::VcsRevisionAlgorithm::Sha256)) {
            digestLength = 32;
          }
          if (digestLength != 0) {
            auto revisionBytes = decoder.decodeBytes(digestLength);
            auto segments = decodeSegments(decoder);
            ZC_IF_SOME(revisionValueBytes, revisionBytes) {
              ZC_IF_SOME(segmentValues, segments) {
                auto canonicalUrl = identity::CanonicalUrl::fromCanonical(urlValue);
                auto revision = identity::VcsRevision::from(
                    static_cast<identity::VcsRevisionAlgorithm>(algorithmValue),
                    revisionValueBytes);
                ZC_IF_SOME(admittedUrl, canonicalUrl) {
                  ZC_IF_SOME(admittedRevision, revision) {
                    return identity::CanonicalPackageSource::vcs(
                        zc::mv(admittedUrl), zc::mv(admittedRevision),
                        identity::CanonicalRelativePath::from(zc::mv(segmentValues)));
                  }
                }
              }
            }
          }
        }
      }
      return LockIssue::SourceKeyMismatch;
    }
    if (tagValue == static_cast<uint8_t>(identity::PackageSourceKind::LocalPath)) {
      auto parents = decoder.decodeUint32();
      auto segments = decodeSegments(decoder);
      ZC_IF_SOME(parentValue, parents) {
        ZC_IF_SOME(segmentValues, segments) {
          return identity::CanonicalPackageSource::localPath(
              identity::CanonicalWorkspaceRelativePath::from(parentValue, zc::mv(segmentValues)));
        }
      }
      return LockIssue::SourceKeyMismatch;
    }
  }
  return LockIssue::SourceKeyMismatch;
}

SourceDecodeResult decodeSource(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto result = decodeSource(decoder);
  if (result.is<identity::CanonicalPackageSource>() && !decoder.finished()) {
    return LockIssue::SourceKeyMismatch;
  }
  return result;
}

using KeyDecodeResult = zc::OneOf<identity::PackageKey, LockIssue>;

KeyDecodeResult decodePackageKey(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto source = decodeSource(decoder);
  if (source.is<LockIssue>()) { return source.get<LockIssue>(); }
  auto nameText = decodeText(decoder, 255);
  auto versionText = decodeText(decoder, 255);
  auto featureCount = decoder.decodeSequenceSize(100000);
  ZC_IF_SOME(nameValue, nameText) {
    ZC_IF_SOME(versionValue, versionText) {
      ZC_IF_SOME(featureCountValue, featureCount) {
        auto name = identity::PackageName::fromCanonical(nameValue);
        auto version = identity::ResolvedVersion::fromCanonical(versionValue);
        zc::Vector<identity::FeatureName> features;
        for (uint64_t index = 0; index < featureCountValue; ++index) {
          auto featureText = decodeText(decoder, 255);
          ZC_IF_SOME(featureTextValue, featureText) {
            auto feature = identity::FeatureName::fromCanonical(featureTextValue);
            ZC_IF_SOME(featureValue, feature) {
              features.add(zc::mv(featureValue));
              continue;
            }
          }
          return LockIssue::PackageKeyMismatch;
        }
        auto sortedFeatures = identity::SortedFeatureSet::from(zc::mv(features));
        ZC_IF_SOME(nameResult, name) {
          ZC_IF_SOME(versionResult, version) {
            ZC_IF_SOME(featureResult, sortedFeatures) {
              if (decoder.finished()) {
                auto key = identity::PackageKey::from(
                    zc::mv(source.get<identity::CanonicalPackageSource>()), zc::mv(nameResult),
                    zc::mv(versionResult), zc::mv(featureResult));
                if (key.encode().asPtr() == bytes) { return key; }
              }
            }
          }
        }
      }
    }
  }
  return LockIssue::PackageKeyMismatch;
}

zc::Maybe<zc::String> parseQuotedValue(zc::StringPtr line, zc::StringPtr field) {
  const auto prefix = zc::str(field, " = \""_zc);
  if (!line.startsWith(prefix) || !line.endsWith("\""_zc) || line.size() < prefix.size() + 1) {
    return zc::none;
  }
  auto value = line.slice(prefix.size(), line.size() - 1);
  if (value.findFirst('"') != zc::none || value.findFirst('\\') != zc::none) { return zc::none; }
  return zc::heapString(value);
}

zc::Maybe<identity::SortedFeatureSet> parseFeatures(zc::StringPtr line) {
  if (!line.startsWith("features = ["_zc) || !line.endsWith("]"_zc)) { return zc::none; }
  auto body = line.slice(12, line.size() - 1);
  zc::Vector<identity::FeatureName> features;
  if (body.size() != 0) {
    size_t cursor = 0;
    while (cursor < body.size()) {
      if (body[cursor] != '"') { return zc::none; }
      const size_t start = ++cursor;
      while (cursor < body.size() && body[cursor] != '"') {
        if (body[cursor] == '\\') { return zc::none; }
        ++cursor;
      }
      if (cursor == body.size()) { return zc::none; }
      auto featureText = zc::heapString(body.slice(start, cursor));
      auto feature = identity::FeatureName::fromCanonical(featureText);
      ZC_IF_SOME(value, feature) { features.add(zc::mv(value)); }
      else { return zc::none; }
      ++cursor;
      if (cursor == body.size()) { break; }
      if (cursor + 2 > body.size() || body.slice(cursor, cursor + 2) != ", "_zc) {
        return zc::none;
      }
      cursor += 2;
    }
  }
  return identity::SortedFeatureSet::from(zc::mv(features));
}

zc::Vector<zc::String> splitLines(zc::StringPtr source) {
  zc::Vector<zc::String> lines;
  size_t start = 0;
  for (size_t index = 0; index < source.size(); ++index) {
    if (source[index] != '\n') { continue; }
    lines.add(zc::heapString(source.slice(start, index)));
    start = index + 1;
  }
  if (start < source.size()) { lines.add(zc::heapString(source.slice(start))); }
  return lines;
}

template <typename Value>
Value& requireMaybe(zc::Maybe<Value>& value) {
  ZC_IF_SOME(item, value) { return item; }
  ZC_UNREACHABLE
}

}  // namespace

LockPackageRecord::LockPackageRecord(identity::PackageKey&& key,
                                     const identity::Sha256Digest& manifestDigest,
                                     const identity::Sha256Digest& sourceTreeDigest,
                                     zc::Maybe<ArchiveFormat> archiveFormat,
                                     zc::Maybe<identity::Sha256Digest> archiveDigest,
                                     zc::Maybe<SigningKeyId> signingKey) noexcept
    : keyValue(zc::mv(key)),
      manifestDigestValue(manifestDigest),
      sourceTreeDigestValue(sourceTreeDigest),
      archiveFormatValue(zc::mv(archiveFormat)),
      archiveDigestValue(zc::mv(archiveDigest)),
      signingKeyValue(zc::mv(signingKey)) {}

zc::Maybe<LockPackageRecord> LockPackageRecord::from(
    identity::PackageKey&& key, const identity::Sha256Digest& manifestDigest,
    const identity::Sha256Digest& sourceTreeDigest, zc::Maybe<ArchiveFormat> archiveFormat,
    zc::Maybe<identity::Sha256Digest> archiveDigest, zc::Maybe<SigningKeyId> signingKey) {
  const bool registry = key.source().kind() == identity::PackageSourceKind::Registry;
  const bool completeArchive =
      archiveFormat != zc::none && archiveDigest != zc::none && signingKey != zc::none;
  if (registry != completeArchive) { return zc::none; }
  return LockPackageRecord(zc::mv(key), manifestDigest, sourceTreeDigest, zc::mv(archiveFormat),
                           zc::mv(archiveDigest), zc::mv(signingKey));
}

LockPackageRecord LockPackageRecord::clone() const {
  auto result = from(keyValue.clone(), manifestDigestValue, sourceTreeDigestValue,
                     cloneArchiveFormat(archiveFormatValue), cloneDigest(archiveDigestValue),
                     cloneSigningKey(signingKeyValue));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}
const identity::PackageKey& LockPackageRecord::key() const noexcept { return keyValue; }
const identity::Sha256Digest& LockPackageRecord::manifestDigest() const noexcept {
  return manifestDigestValue;
}
const identity::Sha256Digest& LockPackageRecord::sourceTreeDigest() const noexcept {
  return sourceTreeDigestValue;
}
bool LockPackageRecord::hasArchive() const noexcept { return archiveFormatValue != zc::none; }
ArchiveFormat LockPackageRecord::archiveFormat() const {
  ZC_IF_SOME(value, archiveFormatValue) { return value; }
  ZC_UNREACHABLE
}
const identity::Sha256Digest& LockPackageRecord::archiveDigest() const {
  ZC_IF_SOME(value, archiveDigestValue) { return value; }
  ZC_UNREACHABLE
}
const SigningKeyId& LockPackageRecord::signingKey() const {
  ZC_IF_SOME(value, signingKeyValue) { return value; }
  ZC_UNREACHABLE
}
void LockPackageRecord::encode(identity::CanonicalEncoder& encoder) const {
  keyValue.encode(encoder);
  encoder.encodeDigest(manifestDigestValue);
  encoder.encodeDigest(sourceTreeDigestValue);
  if (hasArchive()) {
    encoder.encodeSome();
    encoder.encodeUint8(static_cast<uint8_t>(archiveFormat()));
    encoder.encodeDigest(archiveDigest());
    signingKey().encode(encoder);
  } else {
    encoder.encodeNone();
  }
}

VerifiedLockGraph::VerifiedLockGraph(
    zc::Vector<LockPackageRecord>&& packages,
    zc::Vector<identity::PackageDependencyEdgeKey>&& edges) noexcept
    : packageValues(zc::mv(packages)), edgeValues(zc::mv(edges)) {}

zc::OneOf<VerifiedLockGraph, LockIssue> VerifiedLockGraph::from(
    zc::Vector<LockPackageRecord>&& packages,
    zc::Vector<identity::PackageDependencyEdgeKey>&& edges) {
  packages = canonicalSort(zc::mv(packages));
  edges = canonicalSort(zc::mv(edges));
  for (size_t index = 1; index < packages.size(); ++index) {
    if (encodePackageKey(packages[index - 1].key()).asPtr() ==
        encodePackageKey(packages[index].key()).asPtr()) {
      return LockIssue::DuplicatePackageKey;
    }
  }
  for (size_t index = 1; index < edges.size(); ++index) {
    if (edges[index - 1].encode().asPtr() == edges[index].encode().asPtr()) {
      return LockIssue::DuplicateEdge;
    }
  }
  for (const auto& edge : edges) {
    if (!containsPackage(packages, edge.consumer()) ||
        !containsPackage(packages, edge.provider())) {
      return LockIssue::DanglingEdge;
    }
  }
  return VerifiedLockGraph(zc::mv(packages), zc::mv(edges));
}
zc::ArrayPtr<const LockPackageRecord> VerifiedLockGraph::packages() const noexcept {
  return packageValues;
}
zc::ArrayPtr<const identity::PackageDependencyEdgeKey> VerifiedLockGraph::edges() const noexcept {
  return edgeValues;
}
VerifiedLockGraph VerifiedLockGraph::clone() const {
  zc::Vector<LockPackageRecord> packages;
  for (const auto& package : packageValues) { packages.add(package.clone()); }
  zc::Vector<identity::PackageDependencyEdgeKey> edges;
  for (const auto& edge : edgeValues) { edges.add(edge.clone()); }
  return VerifiedLockGraph(zc::mv(packages), zc::mv(edges));
}
void VerifiedLockGraph::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(packageValues.size());
  for (const auto& package : packageValues) { package.encode(encoder); }
  encoder.encodeSequenceSize(edgeValues.size());
  for (const auto& edge : edgeValues) { edge.encode(encoder); }
}
zc::Array<uint8_t> VerifiedLockGraph::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

zc::OneOf<VerifiedLockGraph, LockIssue> LockedReplayVerifier::replay(
    const VerifiedLockGraph& locked, const VerifiedLockGraph& currentInput,
    zc::ArrayPtr<const identity::RegistryIdentity> trustedRegistries, LockReplayMetrics& metrics) {
  metrics = {};
  for (const auto& package : locked.packages()) {
    ++metrics.packageVisits;
    if (package.key().source().kind() != identity::PackageSourceKind::Registry) { continue; }
    identity::CanonicalEncoder packageRegistry;
    package.key().source().registryIdentity().encode(packageRegistry);
    const auto packageRegistryBytes = packageRegistry.finish();
    bool trusted = false;
    for (const auto& registry : trustedRegistries) {
      identity::CanonicalEncoder trustedRegistry;
      registry.encode(trustedRegistry);
      if (trustedRegistry.finish().asPtr() == packageRegistryBytes.asPtr()) {
        trusted = true;
        break;
      }
    }
    if (!trusted) { return LockIssue::TrustDomainMismatch; }
  }
  for (const auto& edge : locked.edges()) {
    static_cast<void>(edge);
    ++metrics.edgeVisits;
  }
  if (locked.encode().asPtr() != currentInput.encode().asPtr()) {
    return LockIssue::CurrentInputMismatch;
  }
  return locked.clone();
}

zc::String LockfileCodec::write(const VerifiedLockGraph& graph) {
  zc::Vector<char> output;
  appendField(output, "schema"_zc, "zom-lock-1"_zc);
  for (const auto& package : graph.packages()) {
    output.add('\n');
    append(output, "[[package]]\n"_zc);
    appendField(output, "key"_zc, zc::encodeHex(package.key().encode()));
    appendField(output, "source-kind"_zc, sourceKindName(package.key().source().kind()));
    identity::CanonicalEncoder sourceEncoder;
    package.key().source().encode(sourceEncoder);
    appendField(output, "source-key"_zc, zc::encodeHex(sourceEncoder.finish()));
    appendField(output, "name"_zc, package.key().name());
    appendField(output, "version"_zc, package.key().version());
    appendFeatures(output, package.key().features());
    appendField(output, "manifest-sha256"_zc, zc::encodeHex(package.manifestDigest().bytes()));
    appendField(output, "source-tree-sha256"_zc, zc::encodeHex(package.sourceTreeDigest().bytes()));
    if (package.hasArchive()) {
      appendField(output, "archive-format"_zc, "tar-zstd-v1"_zc);
      appendField(output, "archive-sha256"_zc, zc::encodeHex(package.archiveDigest().bytes()));
      appendField(output, "signing-key"_zc, zc::encodeHex(package.signingKey().digest().bytes()));
    }
    const auto consumerKey = package.key().encode();
    for (const auto& edge : graph.edges()) {
      if (edge.consumer().encode().asPtr() != consumerKey.asPtr()) { continue; }
      output.add('\n');
      append(output, "[[package.dependency]]\n"_zc);
      appendField(output, "domain"_zc, dependencyDomainName(edge.domain()));
      appendField(output, "alias"_zc, edge.alias());
      appendField(output, "target-key"_zc, zc::encodeHex(edge.provider().encode()));
    }
  }
  return zc::str(output.releaseAsArray());
}

LockParseResult LockfileCodec::parse(zc::ArrayPtr<const zc::byte> source) {
  if (source.size() == 0 || source.back() != static_cast<zc::byte>('\n')) {
    return LockIssue::NonCanonicalEncoding;
  }
  auto validated = zc::encodeUtf32(source.asChars());
  if (validated == zc::none) { return LockIssue::InvalidUtf8; }
  for (zc::byte byte : source) {
    if (byte == static_cast<zc::byte>('\r')) { return LockIssue::NonCanonicalEncoding; }
  }
  const auto sourceText = zc::heapString(source.asChars());
  const auto lines = splitLines(sourceText);
  if (lines.size() == 0) { return LockIssue::TomlSyntax; }
  if (lines[0] != "schema = \"zom-lock-1\""_zc) {
    if (lines[0].startsWith("schema = "_zc)) { return LockIssue::UnsupportedSchema; }
    return LockIssue::MissingField;
  }
  const zc::StringPtr allowedFields[] = {
      "key = "_zc,
      "source-kind = "_zc,
      "source-key = "_zc,
      "name = "_zc,
      "version = "_zc,
      "features = "_zc,
      "manifest-sha256 = "_zc,
      "source-tree-sha256 = "_zc,
      "archive-format = "_zc,
      "archive-sha256 = "_zc,
      "signing-key = "_zc,
      "domain = "_zc,
      "alias = "_zc,
      "target-key = "_zc,
  };
  for (size_t index = 1; index < lines.size(); ++index) {
    if (lines[index].size() == 0 || lines[index] == "[[package]]"_zc ||
        lines[index] == "[[package.dependency]]"_zc) {
      continue;
    }
    bool allowed = false;
    for (zc::StringPtr field : allowedFields) {
      if (lines[index].startsWith(field)) {
        allowed = true;
        break;
      }
    }
    if (!allowed) { return LockIssue::UnknownField; }
  }

  struct PendingEdge final {
    identity::PackageKey consumer;
    zc::String domain;
    zc::String alias;
    zc::Array<uint8_t> targetKey;
  };
  zc::Vector<LockPackageRecord> packages;
  zc::Vector<PendingEdge> pendingEdges;
  size_t cursor = 1;
  while (cursor < lines.size()) {
    if (lines[cursor++] != ""_zc) { return LockIssue::NonCanonicalEncoding; }
    if (cursor == lines.size() || lines[cursor++] != "[[package]]"_zc) {
      return LockIssue::TomlSyntax;
    }
    auto takeField = [&](zc::StringPtr name) -> zc::Maybe<zc::String> {
      if (cursor == lines.size()) { return zc::none; }
      auto result = parseQuotedValue(lines[cursor], name);
      if (result != zc::none) { ++cursor; }
      return result;
    };

    auto keyText = takeField("key"_zc);
    auto sourceKind = takeField("source-kind"_zc);
    auto sourceKeyText = takeField("source-key"_zc);
    auto nameText = takeField("name"_zc);
    auto versionText = takeField("version"_zc);
    if (keyText == zc::none || sourceKind == zc::none || sourceKeyText == zc::none ||
        nameText == zc::none || versionText == zc::none || cursor == lines.size()) {
      return LockIssue::MissingField;
    }
    auto parsedFeatures = parseFeatures(lines[cursor++]);
    auto manifestText = takeField("manifest-sha256"_zc);
    auto sourceTreeText = takeField("source-tree-sha256"_zc);
    if (parsedFeatures == zc::none || manifestText == zc::none || sourceTreeText == zc::none) {
      return LockIssue::WrongValueType;
    }

    auto keyBytes = decodeLowerHex(requireMaybe(keyText));
    auto sourceKeyBytes = decodeLowerHex(requireMaybe(sourceKeyText));
    auto manifestDigest = parseDigest(requireMaybe(manifestText));
    auto sourceTreeDigest = parseDigest(requireMaybe(sourceTreeText));
    if (keyBytes == zc::none || sourceKeyBytes == zc::none || manifestDigest == zc::none ||
        sourceTreeDigest == zc::none) {
      return LockIssue::InvalidDigest;
    }
    auto decodedKey = decodePackageKey(requireMaybe(keyBytes));
    auto decodedSource = decodeSource(requireMaybe(sourceKeyBytes));
    if (decodedKey.is<LockIssue>()) { return decodedKey.get<LockIssue>(); }
    if (decodedSource.is<LockIssue>()) { return decodedSource.get<LockIssue>(); }
    auto& key = decodedKey.get<identity::PackageKey>();
    identity::CanonicalEncoder sourceEncoder;
    key.source().encode(sourceEncoder);
    if (sourceEncoder.finish().asPtr() != requireMaybe(sourceKeyBytes).asPtr()) {
      return LockIssue::SourceKeyMismatch;
    }
    ZC_IF_SOME(sourceKindValue, sourceKind) {
      if (sourceKindName(key.source().kind()) != sourceKindValue ||
          key.name() != requireMaybe(nameText) || key.version() != requireMaybe(versionText)) {
        return LockIssue::SourceFieldMismatch;
      }
    }
    ZC_IF_SOME(featureValues, parsedFeatures) {
      identity::CanonicalEncoder expectedFeatures;
      identity::CanonicalEncoder actualFeatures;
      featureValues.encode(expectedFeatures);
      zc::Vector<identity::FeatureName> keyFeatureValues;
      for (const auto& feature : key.features()) { keyFeatureValues.add(feature.clone()); }
      auto keyFeatureSet = identity::SortedFeatureSet::from(zc::mv(keyFeatureValues));
      ZC_IF_SOME(keyFeatures, keyFeatureSet) { keyFeatures.encode(actualFeatures); }
      if (expectedFeatures.finish().asPtr() != actualFeatures.finish().asPtr()) {
        return LockIssue::PackageKeyMismatch;
      }
    }

    zc::Maybe<ArchiveFormat> archiveFormat;
    zc::Maybe<identity::Sha256Digest> archiveDigest;
    zc::Maybe<SigningKeyId> signingKey;
    if (key.source().kind() == identity::PackageSourceKind::Registry) {
      auto archiveFormatText = takeField("archive-format"_zc);
      auto archiveDigestText = takeField("archive-sha256"_zc);
      auto signingKeyText = takeField("signing-key"_zc);
      if (archiveFormatText == zc::none || archiveDigestText == zc::none ||
          signingKeyText == zc::none || requireMaybe(archiveFormatText) != "tar-zstd-v1"_zc) {
        return LockIssue::SourceFieldMismatch;
      }
      archiveFormat = ArchiveFormat::TarZstdV1;
      archiveDigest = parseDigest(requireMaybe(archiveDigestText));
      auto signingDigest = parseDigest(requireMaybe(signingKeyText));
      ZC_IF_SOME(value, signingDigest) { signingKey = SigningKeyId::fromDigest(value); }
      if (archiveDigest == zc::none || signingKey == zc::none) { return LockIssue::InvalidDigest; }
    } else if (cursor < lines.size() && (lines[cursor].startsWith("archive-"_zc) ||
                                         lines[cursor].startsWith("signing-key"_zc))) {
      return LockIssue::SourceFieldMismatch;
    }

    auto package = LockPackageRecord::from(zc::mv(key), requireMaybe(manifestDigest),
                                           requireMaybe(sourceTreeDigest), zc::mv(archiveFormat),
                                           zc::mv(archiveDigest), zc::mv(signingKey));
    if (package == zc::none) { return LockIssue::SourceFieldMismatch; }
    ZC_IF_SOME(packageValue, package) {
      packages.add(zc::mv(packageValue));
      while (cursor < lines.size()) {
        if (lines[cursor] != ""_zc) { return LockIssue::NonCanonicalEncoding; }
        if (cursor + 1 == lines.size()) { return LockIssue::NonCanonicalEncoding; }
        if (lines[cursor + 1] == "[[package]]"_zc) { break; }
        if (lines[cursor + 1] != "[[package.dependency]]"_zc) { return LockIssue::UnknownField; }
        cursor += 2;
        auto domain = takeField("domain"_zc);
        auto alias = takeField("alias"_zc);
        auto targetKeyText = takeField("target-key"_zc);
        if (domain == zc::none || alias == zc::none || targetKeyText == zc::none) {
          return LockIssue::MissingField;
        }
        auto targetKey = decodeLowerHex(requireMaybe(targetKeyText));
        if (targetKey == zc::none) { return LockIssue::PackageKeyMismatch; }
        pendingEdges.add(PendingEdge{packages.back().key().clone(), zc::mv(requireMaybe(domain)),
                                     zc::mv(requireMaybe(alias)), zc::mv(requireMaybe(targetKey))});
      }
    }
  }

  zc::Vector<identity::PackageDependencyEdgeKey> edges;
  for (auto& pending : pendingEdges) {
    size_t providerIndex = packages.size();
    for (size_t index = 0; index < packages.size(); ++index) {
      if (packages[index].key().encode().asPtr() == pending.targetKey.asPtr()) {
        providerIndex = index;
        break;
      }
    }
    if (providerIndex == packages.size()) { return LockIssue::DanglingEdge; }
    identity::DependencyDomain domain;
    if (pending.domain == "target"_zc) {
      domain = identity::DependencyDomain::Target;
    } else if (pending.domain == "development"_zc) {
      domain = identity::DependencyDomain::Development;
    } else if (pending.domain == "build"_zc) {
      domain = identity::DependencyDomain::Build;
    } else {
      return LockIssue::WrongValueType;
    }
    auto alias = identity::DependencyAlias::fromCanonical(pending.alias);
    ZC_IF_SOME(aliasValue, alias) {
      auto edge =
          identity::PackageDependencyEdgeKey::from(zc::mv(pending.consumer), zc::mv(aliasValue),
                                                   domain, packages[providerIndex].key().clone());
      ZC_IF_SOME(edgeValue, edge) {
        edges.add(zc::mv(edgeValue));
        continue;
      }
    }
    return LockIssue::WrongValueType;
  }

  auto graph = VerifiedLockGraph::from(zc::mv(packages), zc::mv(edges));
  if (graph.is<LockIssue>()) { return graph.get<LockIssue>(); }
  auto& verified = graph.get<VerifiedLockGraph>();
  if (write(verified).asBytes() != source) { return LockIssue::NonCanonicalEncoding; }
  return zc::mv(verified);
}

LockParseResult LockfileCodec::read(const zc::ReadableDirectory& workspaceRoot) {
  try {
    auto file = workspaceRoot.openFile(zc::Path("Zom.lock"_zc));
    const auto bytes = file->readAllBytes();
    return parse(bytes);
  } catch (const zc::Exception&) { return LockIssue::ReadFailed; }
}

zc::Maybe<LockWriteStage> AtomicLockfileWriter::write(const zc::Directory& workspaceRoot,
                                                      zc::StringPtr canonicalLockfile,
                                                      zc::Maybe<LockWriteStage> injectedFailure) {
  LockWriteStage stage = LockWriteStage::TemporaryCreate;
  try {
    if (sameInjected(injectedFailure, stage)) { return stage; }
    auto replacement = workspaceRoot.replaceFile(zc::Path("Zom.lock"_zc),
                                                 zc::WriteMode::CREATE | zc::WriteMode::MODIFY);
    stage = LockWriteStage::Write;
    if (sameInjected(injectedFailure, stage)) { return stage; }
    replacement->get().writeAll(canonicalLockfile);
    stage = LockWriteStage::FileSync;
    if (sameInjected(injectedFailure, stage)) { return stage; }
    replacement->get().sync();
    stage = LockWriteStage::Rename;
    if (sameInjected(injectedFailure, stage)) { return stage; }
    replacement->commit();
    stage = LockWriteStage::DirectorySync;
    if (sameInjected(injectedFailure, stage)) { return stage; }
    workspaceRoot.sync();
    return zc::none;
  } catch (const zc::Exception&) { return stage; }
}

}  // namespace zomlang::compiler::driver::package
