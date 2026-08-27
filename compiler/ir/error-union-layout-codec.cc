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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compiler/ir/error-union-layout-codec.h"

#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::ir {
namespace {

/// \brief Interprets 32 raw bytes as a digest, failing closed on a wrong length.
identity::Sha256Digest toDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::Sha256Digest::fromBytes(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE
}

/// \brief Computes the SHA-256 digest of `bytes`, failing closed on error.
identity::Sha256Digest hashOf(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE
}

/// \brief Emits `domain` characters as individual bytes followed by a NUL.
///
/// Mirrors the OwnershipFactsCodec domain-separation idiom exactly.
void encodeDomain(identity::CanonicalEncoder& encoder, const char* domain, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0x00);
}

/// \brief Encodes the interior of one descriptor (no domain prefix).
///
/// Fields encode in RFC 0006 declaration order.
void encodeDescriptorBody(identity::CanonicalEncoder& encoder,
                          const ErrorUnionLayoutDescriptor& descriptor) {
  encoder.encodeByteString(descriptor.valueTypeKey.asPtr());
  encoder.encodeByteString(descriptor.successTypeKey.asPtr());
  encoder.encodeSequenceSize(descriptor.residualTypeKeys.size());
  for (const auto& key : descriptor.residualTypeKeys) { encoder.encodeByteString(key.asPtr()); }
  encoder.encodeDigest(toDigest(descriptor.checkedFactsRevision.asPtr()));
  encoder.encodeDigest(toDigest(descriptor.dispatchFactsRevision.asPtr()));
  encoder.encodeDigest(toDigest(descriptor.targetSpecId.asPtr()));
  encoder.encodeUint8(static_cast<uint8_t>(descriptor.tagWidth));
  encoder.encodeUint64(descriptor.tagOffset);
  encoder.encodeUint64(descriptor.payloadOffset);
  encoder.encodeUint64(descriptor.payloadSize);
  encoder.encodeUint64(descriptor.payloadAlign);
  encoder.encodeUint64(descriptor.size);
  encoder.encodeUint64(descriptor.align);
  encoder.encodeSequenceSize(descriptor.alternatives.size());
  for (const auto& alternative : descriptor.alternatives) {
    encoder.encodeUint64(alternative.tag);
    encoder.encodeByteString(alternative.typeKey.asPtr());
    encoder.encodeUint8(static_cast<uint8_t>(alternative.kind));
    encoder.encodeUint64(alternative.payloadSize);
    encoder.encodeUint64(alternative.payloadAlign);
  }
}

}  // namespace

zc::Array<uint8_t> ErrorUnionLayoutCodec::encode(const ErrorUnionLayoutDescriptor& descriptor) {
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.error-union-layout";
  encodeDomain(encoder, domain, sizeof(domain) - 1);
  encodeDescriptorBody(encoder, descriptor);
  return encoder.finish();
}

ErrorUnionLayoutRevision ErrorUnionLayoutCodec::compute(
    const ErrorUnionLayoutDescriptor& descriptor) {
  auto bytes = encode(descriptor);
  return ErrorUnionLayoutRevision::fromDigest(hashOf(bytes.asPtr()));
}

zc::Array<uint8_t> ErrorUnionLayoutCodec::encodeManifest(
    const TargetArtifactAbiManifest& manifest) {
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.target-artifact-abi";
  encodeDomain(encoder, domain, sizeof(domain) - 1);
  encoder.encodeByteString(manifest.moduleKey.asPtr());
  encoder.encodeDigest(toDigest(manifest.interfaceRevision.asPtr()));
  encoder.encodeDigest(toDigest(manifest.targetSpecId.asPtr()));
  encoder.encodeSequenceSize(manifest.errorUnionLayouts.size());
  for (const auto& layout : manifest.errorUnionLayouts) {
    encoder.encodeByteString(layout.encodedDescriptor.asPtr());
    encoder.encodeDigest(toDigest(layout.revision.asPtr()));
  }
  return encoder.finish();
}

TargetArtifactAbiRevision ErrorUnionLayoutCodec::computeManifestRevision(
    const TargetArtifactAbiManifest& manifest) {
  auto bytes = encodeManifest(manifest);
  return TargetArtifactAbiRevision::fromDigest(hashOf(bytes.asPtr()));
}

}  // namespace zomlang::compiler::ir
